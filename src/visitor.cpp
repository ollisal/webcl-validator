#include "visitor.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/Frontend/CompilerInstance.h"

WebCLRestrictor::WebCLRestrictor(clang::CompilerInstance &instance)
    : WebCLReporter(instance)
    , clang::RecursiveASTVisitor<WebCLRestrictor>()
{
}

WebCLRestrictor::~WebCLRestrictor()
{
}

bool WebCLRestrictor::VisitFunctionDecl(clang::FunctionDecl *decl)
{
    if (decl->hasAttr<clang::OpenCLKernelAttr>()) {
        const clang::DeclarationNameInfo nameInfo = decl->getNameInfo();
        const clang::IdentifierInfo *idInfo = nameInfo.getName().getAsIdentifierInfo();
        if (!idInfo) {
            error(nameInfo.getLoc(),
                  "Invalid kernel name.\n");
        } else if (idInfo->getLength() > 255) {
            error(nameInfo.getLoc(),
                  "WebCL restricts kernel name lengths to 255 characters.\n");
        }
    }

    return true;
}

bool WebCLRestrictor::VisitParmVarDecl(clang::ParmVarDecl *decl)
{ 
    const clang::TypeSourceInfo *info = decl->getTypeSourceInfo();
    if (!info) {
        error(decl->getSourceRange().getBegin(), "Invalid parameter type.\n");
        return true;
    }

    clang::SourceLocation typeLocation = info->getTypeLoc().getBeginLoc();

    const clang::Type *type = info->getType().getTypePtrOrNull();
    if (!info) {
        error(typeLocation, "Invalid parameter type.\n");
        return true;
    }
    const clang::DeclContext *context = decl->getParentFunctionOrMethod();
    if (!context) {
        error(typeLocation, "Invalid parameter context.\n");
        return true;
    }
    clang::FunctionDecl *function = clang::FunctionDecl::castFromDeclContext(context);
    if (!function) {
        error(typeLocation, "Invalid parameter context.\n");
        return true;
    }

    checkStructureParameter(function, typeLocation, type);
    check3dImageParameter(function, typeLocation, type);
    return true;
}

void WebCLRestrictor::checkStructureParameter(
    clang::FunctionDecl *decl,
    clang::SourceLocation typeLocation, const clang::Type *type)
{
    if (decl->hasAttr<clang::OpenCLKernelAttr>() &&
        type->isRecordType()) {
        error(typeLocation, "WebCL doesn't support structures as kernel parameters.\n");
    }
}

void WebCLRestrictor::check3dImageParameter(
    clang::FunctionDecl *decl,
    clang::SourceLocation typeLocation, const clang::Type *type)
{
    if (!isFromMainFile(typeLocation))
        return;

    clang::QualType canonical = type->getCanonicalTypeInternal();
    type = canonical.getTypePtrOrNull();
    if (!type) {
        error(typeLocation, "Invalid canonical type.");
    } else if (type->isPointerType()) {
        clang::QualType pointee = type->getPointeeType();
        if (pointee.getAsString() == "struct image3d_t_")
            error(typeLocation, "WebCL doesn't support 3D images.\n");
    }
}

WebCLAccessor::WebCLAccessor(
    clang::CompilerInstance &instance)
    : WebCLReporter(instance)
    , WebCLTransformerClient()
    , clang::RecursiveASTVisitor<WebCLAccessor>()
{
}

WebCLAccessor::~WebCLAccessor()
{
}

bool WebCLAccessor::VisitArraySubscriptExpr(clang::ArraySubscriptExpr *expr)
{
    if (!isFromMainFile(expr->getLocStart()))
        return true;

    const clang::Expr *base = expr->getBase();
    const clang::Expr *index = expr->getIdx();

    llvm::APSInt arraySize;
    const bool isArraySizeKnown = getIndexedArraySize(base, arraySize);
    bool isArraySizeValid = false;
    if (isArraySizeKnown) {
        if (arraySize.getActiveBits() >= 63) {
            error(base->getLocStart(), "Invalid array size.\n");
        } else {
            isArraySizeValid = arraySize.isStrictlyPositive();
            if (!isArraySizeValid)
                error(base->getLocStart(), "Array size is not positive.\n");
        }
    } else {
        warning(base->getLocStart(), "Array size not known until run-time.\n");
    }

    llvm::APSInt indexValue;
    const bool isIndexValueKnown = getArrayIndexValue(index, indexValue);
    bool isIndexValueValid = false;
    if (isIndexValueKnown) {
        if (indexValue.getActiveBits() >= 63) {
            error(index->getLocStart(), "Invalid array index.\n");
        } else {
            isIndexValueValid = !indexValue.isNegative();
            if (!isIndexValueValid)
                error(index->getLocStart(), "Array index is too small.\n");
        }
    } else {
        warning(index->getLocStart(), "Index value not known until run-time.\n");
    }

    if (isArraySizeValid && isIndexValueValid) {
        const unsigned int arraySizeWidth = arraySize.getBitWidth();
        const unsigned int indexValueWidth = indexValue.getBitWidth();
        if (arraySizeWidth < indexValueWidth) {
            arraySize = arraySize.zext(indexValueWidth);
        } else if (indexValueWidth < arraySizeWidth) {
            indexValue = indexValue.zext(arraySizeWidth);
        }
        if (indexValue.uge(arraySize))
            error(index->getLocStart(), "Array index is too large.\n");
    }

    if (isArraySizeValid && !isIndexValueValid) {
        getTransformer().addArrayIndexCheck(expr, arraySize);
    }

    return true;
}

bool WebCLAccessor::VisitUnaryOperator(clang::UnaryOperator *expr)
{
    if (!isFromMainFile(expr->getLocStart()))
        return true;

    if (expr->getOpcode() != clang::UO_Deref)
        return true;

    if (isPointerCheckNeeded(expr->getSubExpr()))
        warning(expr->getExprLoc(), "Pointer access needs to be checked.\n");

    return true;
}

bool WebCLAccessor::getIndexedArraySize(const clang::Expr *base, llvm::APSInt &size)
{
    const clang::Expr *expr = base->IgnoreImpCasts();
    if (!expr) {
        error(base->getLocStart(), "Invalid array.\n");
        return false;
    }

    const clang::ConstantArrayType *type =
        instance_.getASTContext().getAsConstantArrayType(expr->getType());
    if (!type)
        return false;

    size = type->getSize();
    return true;
}

bool WebCLAccessor::getArrayIndexValue(const clang::Expr *index, llvm::APSInt &value)
{
    // Clang does these checks when checking for integer constant
    // expressions so we'll follow that practice.
    if (index->isTypeDependent() || index->isValueDependent())
        return false;
    // See also Expr::EvaluateAsInt for a more powerful variant of
    // Expr::isIntegerConstantExpr.
    return index->isIntegerConstantExpr(value, instance_.getASTContext());
}

bool WebCLAccessor::isPointerCheckNeeded(const clang::Expr *expr)
{
    const clang::Expr *pointer = expr->IgnoreImpCasts();
    if (!pointer) {
        error(expr->getLocStart(), "Invalid pointer access.\n");
        return false;
    }

    const clang::Type *type = pointer->getType().getTypePtrOrNull();
    if (!type) {
        error(pointer->getLocStart(), "Invalid pointer type.\n");
        return false;
    }

    return type->isPointerType();
}

WebCLPrinter::WebCLPrinter()
    : clang::RecursiveASTVisitor<WebCLPrinter>()
{
}

WebCLPrinter::~WebCLPrinter()
{
}

bool WebCLPrinter::VisitTranslationUnitDecl(clang::TranslationUnitDecl *decl)
{
    clang::ASTContext &context = decl->getASTContext();
    clang::PrintingPolicy policy = context.getPrintingPolicy();
    policy.SuppressAuxFiles = true;
    context.setPrintingPolicy(policy);
    decl->print(llvm::outs(), policy, 0, true);
    return false;
}
