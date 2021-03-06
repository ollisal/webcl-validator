include_directories(${CMAKE_CURRENT_BINARY_DIR})

macro(generate_hex_header name)
  add_custom_command(
    OUTPUT ${name}.h
    DEPENDS ${name}.cl
    COMMAND ${PYTHON_EXECUTABLE}
              ${CMAKE_CURRENT_SOURCE_DIR}/stripcr.py
			  ${CMAKE_CURRENT_SOURCE_DIR}/${name}.cl
			  ${name}-endlfix.cl
    COMMAND xxd
              -i ${name}-endlfix.cl
			  ${name}.h
  )
endmacro(generate_hex_header name)

generate_hex_header(general)
generate_hex_header(kernel)

# Sources for validator library
llvm_process_sources(libclv_srcs
  general.h
  kernel.h
  WebCLAction.cpp
  WebCLArguments.cpp
  WebCLBuiltins.cpp
  WebCLConfiguration.cpp
  WebCLConsumer.cpp
  WebCLHelper.cpp
  WebCLMatcher.cpp
  WebCLPass.cpp
  WebCLPreprocessor.cpp
  WebCLPrinter.cpp
  WebCLRenamer.cpp
  WebCLReporter.cpp
  WebCLRewriter.cpp
  WebCLTool.cpp
  WebCLTransformer.cpp
  WebCLVisitor.cpp
  WebCLTypes.cpp
  clv.cpp
)

# Add public header for IDE
set(libclv_headers
  "${WCLV_SOURCE_DIR}/include/clv/clv.h"
)

if(MSVC_IDE OR XCODE)
  set_source_files_properties(${libclv_headers} PROPERTIES HEADER_FILE_ONLY ON)
  set(libclv_srcs ${libclv_srcs} ${libclv_headers})
endif(MSVC_IDE OR XCODE)

# Build a static library until our ABI is somewhat stable
add_library(libclv STATIC ${libclv_srcs})

# The usual LLVM build system properties
if( LLVM_COMMON_DEPENDS )
  add_dependencies(libclv ${LLVM_COMMON_DEPENDS} )
endif( LLVM_COMMON_DEPENDS )

llvm_config(libclv ${LLVM_LINK_COMPONENTS})
target_link_libraries(libclv ${LLVM_COMMON_LIBS} )
link_system_libs(libclv)

install(TARGETS libclv
  LIBRARY DESTINATION lib${LLVM_LIBDIR_SUFFIX}
  ARCHIVE DESTINATION lib${LLVM_LIBDIR_SUFFIX}
  RUNTIME DESTINATION bin)
set_target_properties(libclv PROPERTIES FOLDER "Clang libraries")

target_link_libraries(libclv
  clangTooling
  clangRewriteFrontend
)
