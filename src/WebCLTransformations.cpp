#include "WebCLTransformation.hpp"
#include "WebCLTransformations.hpp"

#include "clang/AST/Expr.h"

WebCLTransformations::WebCLTransformations(clang::CompilerInstance &instance)
    : WebCLReporter(instance)
    , declTransformations_()
    , exprTransformations_()
{
}

WebCLTransformations::~WebCLTransformations()
{
    deleteTransformations(declTransformations_);
    deleteTransformations(exprTransformations_);
}

void WebCLTransformations::addTransformation(
    clang::Decl *decl, WebCLTransformation *transformation)
{
    addTransformation(declTransformations_, decl, transformation);
}

void WebCLTransformations::addTransformation(
    clang::Expr *expr, WebCLRecursiveTransformation *transformation)
{
    addTransformation(exprTransformations_, expr, transformation);
}

WebCLTransformation* WebCLTransformations::getTransformation(const clang::Decl *decl)
{
    return getTransformation<DeclTransformations, clang::Decl, WebCLTransformation>(
        declTransformations_, decl);
}

WebCLRecursiveTransformation* WebCLTransformations::getTransformation(const clang::Expr *expr)
{
    return getTransformation<ExprTransformations, clang::Expr, WebCLRecursiveTransformation>(
        exprTransformations_, expr);
}

bool WebCLTransformations::rewriteTransformations(
    WebCLTransformerConfiguration &cfg, clang::Rewriter &rewriter)
{
    bool status = true;

    status = status && rewriteTransformations(declTransformations_, cfg, rewriter);
    status = status && rewriteTransformations(exprTransformations_, cfg, rewriter);

    return status;
}

bool WebCLTransformations::contains(clang::Decl *decl)
{
    return declTransformations_.count(decl) > 0;
}

template <typename NodeMap, typename Node, typename NodeTransformation>
void WebCLTransformations::addTransformation(
    NodeMap &map, const Node *node, NodeTransformation *transformation)
{
    if (!transformation) {
        error(node->getLocStart(), "Internal error. Can't create transformation.");
        return;
    }

    const std::pair<typename NodeMap::iterator, bool> status =
        map.insert(typename NodeMap::value_type(node, transformation));

    if (!status.second) {
        error(node->getLocStart(), "Transformation has been already created.");
        return;
    }
}

template <typename NodeMap, typename Node, typename NodeTransformation>
NodeTransformation *WebCLTransformations::getTransformation(NodeMap &map, const Node *node)
{
    typename NodeMap::iterator i = map.find(node);
    if (i == map.end())
        return NULL;
    return i->second;
}

template <typename NodeMap>
void WebCLTransformations::deleteTransformations(NodeMap &map)
{
    for (typename NodeMap::iterator i = map.begin(); i != map.end(); ++i) {
        WebCLTransformation *transformation = i->second;
        delete transformation;
    }
}

template <typename NodeMap>
bool WebCLTransformations::rewriteTransformations(
    NodeMap &map, WebCLTransformerConfiguration &cfg, clang::Rewriter &rewriter)
{
    bool status = true;

    for (typename NodeMap::iterator i = map.begin(); i != map.end(); ++i) {
        WebCLTransformation *transformation = i->second;
        status = status && transformation->rewrite(cfg, rewriter);
    }

    return status;
}
