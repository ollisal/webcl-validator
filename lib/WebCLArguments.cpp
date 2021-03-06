/*
** Copyright (c) 2013 The Khronos Group Inc.
**
** Permission is hereby granted, free of charge, to any person obtaining a
** copy of this software and/or associated documentation files (the
** "Materials"), to deal in the Materials without restriction, including
** without limitation the rights to use, copy, modify, merge, publish,
** distribute, sublicense, and/or sell copies of the Materials, and to
** permit persons to whom the Materials are furnished to do so, subject to
** the following conditions:
**
** The above copyright notice and this permission notice shall be included
** in all copies or substantial portions of the Materials.
**
** THE MATERIALS ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
** EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
** MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
** IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
** CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
** TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
** MATERIALS OR THE USE OR OTHER DEALINGS IN THE MATERIALS.
*/

#include "WebCLArguments.hpp"
#include "kernel.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <set>
#include <fcntl.h>
#include <unistd.h>

// IMPROVEMENT: Maybe we should resolve directory separator and temp directory
//              in windows runtime, since it depends if we are running from msys / cygwin shell
//              or from windows commandline?

#if (defined(__MINGW32__) || defined(_MSC_VER))
#define DIR_SEPARATOR "\\"
#define TEMP_DIR "."
#else
#define DIR_SEPARATOR "/"
#define TEMP_DIR P_tmpdir
#endif

// Windows/MSVC++ doesn't have ssize_t
#ifdef _MSC_VER
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#endif

int mingwcompatible_mkstemp(char* tmplt) {
  char *filename = mktemp(tmplt);
  if (filename == NULL) return -1;
  return open(filename, O_RDWR | O_CREAT, 0600);
}

WebCLArguments::WebCLArguments(const std::string &inputSource, int argc, char const *argv[])
    : preprocessorArgc_(0)
    , preprocessorArgv_(NULL)
    , validatorArgc_(0)
    , validatorArgv_(NULL)
    , matcherArgv_()
    , files_()
    , outputs_()
{
    int inputDescriptor = -1;
    char const *inputFilename = createFullTemporaryFile(inputDescriptor, &inputSource[0], inputSource.size());
    if (!inputFilename)
        return;
    files_.push_back(TemporaryFile(-1, inputFilename));
    close(inputDescriptor);

    char const *buffer = reinterpret_cast<char const*>(kernel_endlfix_cl);
    size_t length = kernel_endlfix_cl_len;
    int headerDescriptor = -1;
    char const *headerFilename = createFullTemporaryFile(headerDescriptor, buffer, length);
    if (!headerFilename)
        return;
    files_.push_back(TemporaryFile(-1, headerFilename));
    close(headerDescriptor);
  
    char const *preprocessorInvocation[] = {
        "libclv", inputFilename, "--"
    };
    const int preprocessorInvocationSize =
        sizeof(preprocessorInvocation) / sizeof(preprocessorInvocation[0]);
    char const *preprocessorOptions[] = {
        "-E", "-x", "cl"
    };
    const int numPreprocessorOptions =
        sizeof(preprocessorOptions) / sizeof(preprocessorOptions[0]);

    char const *validatorInvocation[] = {
        "libclv", NULL, "--"
    };
    const int validatorInvocationSize =
        sizeof(validatorInvocation) / sizeof(validatorInvocation[0]);
    char const *validatorOptions[] = {
        "-x", "cl",
        "-Wno-implicit-function-declaration",
        "-include", headerFilename
    };
    const int numValidatorOptions =
        sizeof(validatorOptions) / sizeof(validatorOptions[0]);

    std::set<char const *> userDefines;
    for (int i = 0; i < argc; ++i) {
        char const *option = argv[i];
        if (!std::string(option).substr(0, 2).compare("-D"))
            userDefines.insert(option);
    }

    preprocessorArgc_ =
        preprocessorInvocationSize + userDefines.size() + numPreprocessorOptions + 1;
    validatorArgc_ =
        validatorInvocationSize + argc + numValidatorOptions + 3;

    preprocessorArgv_ = new char const *[preprocessorArgc_];
    if (!preprocessorArgv_) {
        std::cerr << "Internal error. Can't create argument list for preprocessor."
                  << std::endl;
        return;
    }
    validatorArgv_ = new char const *[validatorArgc_];
    if (!validatorArgv_) {
        std::cerr << "Internal error. Can't create argument list for validator."
                  << std::endl;
        return;
    }

    // preprocessor arguments
    std::copy(preprocessorInvocation,
              preprocessorInvocation + preprocessorInvocationSize,
              preprocessorArgv_);
    std::copy(userDefines.begin(),
              userDefines.end(),
              preprocessorArgv_ + preprocessorInvocationSize);
    std::copy(preprocessorOptions,
              preprocessorOptions + numPreprocessorOptions,
              preprocessorArgv_ + preprocessorInvocationSize + userDefines.size());
    preprocessorArgv_[preprocessorArgc_ - 1] = "-ferror-limit=0";

    // validator arguments
    std::copy(validatorInvocation,
              validatorInvocation + validatorInvocationSize,
              validatorArgv_);
    std::copy(argv,
              argv + argc,
              validatorArgv_ + validatorInvocationSize);
    std::copy(validatorOptions,
              validatorOptions + numValidatorOptions,
              validatorArgv_ + validatorInvocationSize + argc);
    validatorArgv_[validatorArgc_ - 1] = "-ferror-limit=0";
    validatorArgv_[validatorArgc_ - 2] = "-fno-builtin";
    validatorArgv_[validatorArgc_ - 3] = "-ffreestanding";
}

WebCLArguments::~WebCLArguments()
{
    delete[] preprocessorArgv_;
    preprocessorArgv_ = NULL;
    delete[] validatorArgv_;
    validatorArgv_ = NULL;

    while (matcherArgv_.size()) {
        char const **argv = matcherArgv_.back();
        delete[] argv;
        matcherArgv_.pop_back();
    }

    while (files_.size()) {
        TemporaryFile &file = files_.back();
        // close(file.first);
        remove(file.second);
        delete[] file.second;
        files_.pop_back();
    }
}

int WebCLArguments::getPreprocessorArgc() const
{
    if (!areArgumentsOk(preprocessorArgc_, preprocessorArgv_))
        return 0;
    return preprocessorArgc_;
}

int WebCLArguments::getMatcherArgc() const
{
    return getValidatorArgc();
}

int WebCLArguments::getValidatorArgc() const
{
    if (!areArgumentsOk(validatorArgc_, validatorArgv_))
        return 0;
    return validatorArgc_;
}

char const **WebCLArguments::getPreprocessorArgv() const
{
    if (!areArgumentsOk(preprocessorArgc_, preprocessorArgv_))
        return NULL;
    // Input file has been already set.
    return preprocessorArgv_;
}

char const **WebCLArguments::getMatcherArgv()
{
    if (!areArgumentsOk(validatorArgc_, validatorArgv_))
        return NULL;

    char const **matcherArgv = new char const *[validatorArgc_];
    if (!matcherArgv)
        return NULL;
    matcherArgv_.push_back(matcherArgv);
    std::copy(validatorArgv_, validatorArgv_ + validatorArgc_, matcherArgv);

    // Set input file.
    char const *input = outputs_.back();
    matcherArgv[1] = input;

    return matcherArgv;
}

char const **WebCLArguments::getValidatorArgv() const
{
    if (!areArgumentsOk(validatorArgc_, validatorArgv_))
        return NULL;

    // Set input file.
    char const *input = outputs_.back();
    validatorArgv_[1] = input;

    return validatorArgv_;
}

char const *WebCLArguments::getInput(int argc, char const **argv, bool createOutput)
{
    if (!areArgumentsOk(argc, argv))
        return NULL;

    // Create output file for the next tool.
    if (createOutput) {
        int fd = -1;
        char const *name = createEmptyTemporaryFile(fd);
        if (!name)
            return NULL;
        files_.push_back(TemporaryFile(-1, name));
        close(fd);
        outputs_.push_back(name);
    }

    return argv[1];
}

bool WebCLArguments::areArgumentsOk(int argc, char const **argv) const
{
    return (argc > 1) && argv;
}

char const *WebCLArguments::createEmptyTemporaryFile(int &fd) const
{
    char const *directory = TEMP_DIR;
    char const *filename = DIR_SEPARATOR "wclXXXXXX";

    const int directoryLength = strlen(directory);
    const int filenameLength = strlen(filename);
    const int templateLength = directoryLength + filenameLength;

    char *result = new char[templateLength + 1];
    if (!result) {
        std::cerr << "Internal error. Can't create temporary filename." << std::endl;
        return NULL;
    }
 
    std::copy(directory, directory + directoryLength, result);
    std::copy(filename, filename + filenameLength, result + directoryLength);
    result[templateLength] = '\0';

    fd = mingwcompatible_mkstemp(result);
    if (fd == -1) {
        std::cerr << "Internal error. Can't create temporary file." << std::endl;
        delete[] result;
        return NULL;
    }

    return result;
}

char const *WebCLArguments::createCopiedTemporaryFile(int srcFd) const
{
    int dstFd;
    char const *filename = createEmptyTemporaryFile(dstFd);
    if (!filename)
        return NULL;

    char buffer[1024];
    ssize_t rdBytes;
    while ((rdBytes = read(srcFd, buffer, sizeof(buffer))) > 0) {
	const size_t wrBytes = write(dstFd, buffer, rdBytes);
	if (wrBytes != size_t(rdBytes)) {
	    std::cerr << "Internal error. Can't populate temporary file." << std::endl;
	    delete[] filename;
	    close(dstFd);
	    return NULL;
	}
    }
    close(dstFd);
    return filename;
}

char const *WebCLArguments::createFullTemporaryFile(int &fd, char const *buffer, size_t length) const
{
    char const *filename = createEmptyTemporaryFile(fd);
    if (!filename)
        return NULL;

    const size_t bytes = write(fd, buffer, length);
    if (bytes != length) {
        std::cerr << "Internal error. Can't populate temporary file." << std::endl;
        delete[] filename;
        close(fd);
        return NULL;
    }

    return filename;
}
