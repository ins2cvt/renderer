add_executable(invert invert.cpp)
set_target_properties(invert PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_include_directories(invert SYSTEM PRIVATE $ENV{USR_INC})
target_link_directories(invert PRIVATE $ENV{USR_LIB})
add_compile_options(/W4 /utf-8)

if(NOT MSVC)
    message(WARNING "Compilers other than MSVC are untested")
endif()
