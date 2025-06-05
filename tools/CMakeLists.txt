cmake_minimum_required(VERSION 3.14)
project(CPythonCovert)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(nlohmann_json 3.12.0 REQUIRED)
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)

set(AST_SOURCES
  ${PROJECT_SOURCE_DIR}/../src/ast.cpp
  ${PROJECT_SOURCE_DIR}/../src/driver/python/driver.cpp
  ${PROJECT_SOURCE_DIR}/main.cpp
)

add_executable(CPythonCovert ${AST_SOURCES})
target_include_directories(CPythonCovert PRIVATE ${Python3_INCLUDE_DIRS} ${PROJECT_SOURCE_DIR}/../src)
target_link_libraries(CPythonCovert PRIVATE ${Python3_LIBRARIES} nlohmann_json::nlohmann_json)
target_compile_options(CPythonCovert PRIVATE -fsanitize=address,undefined -flto)
target_link_options(CPythonCovert PRIVATE -fsanitize=address,undefined -flto)
