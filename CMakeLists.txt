cmake_minimum_required(VERSION 3.10)
project(data-acquisition-system)

# Configuração da versão do C++
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Configurar política CMP0167
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 OLD)
endif()

# Configuração para Boost
set(Boost_INCLUDE_DIR /usr/include)
set(Boost_LIBRARY_DIR /usr/lib64)

find_package(Boost 1.65.1 REQUIRED COMPONENTS system thread chrono locale)

# Incluir diretórios
include_directories(${Boost_INCLUDE_DIR})

# Criar o executável e vincular as bibliotecas do Boost
add_executable(das src/main.cpp)
target_link_libraries(das ${Boost_LIBRARIES})
