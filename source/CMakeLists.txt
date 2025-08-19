cmake_minimum_required(VERSION 3.14)
project(QtAlp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)    # let Qt handle the moc for signals/slots

# 1) Find exactly the Qt modules we need:
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Network
    WebSockets
    Sql
    Widgets
    Charts
    SerialPort
    DataVisualization
)

find_package(OpenGL REQUIRED)

# 2) List your single source file (you said “everything in main.cpp”):
set(SOURCES
    main.cpp
)

# 3) Create the executable (no separate target_sources calls):
add_executable(QtAlp
    ${SOURCES}
    mainwindow.cpp mainwindow.h
    dvclient.h dvclient.cpp
    scatter3dwidget.h scatter3dwidget.cpp
    comthread.h comthread.cpp
    scatter3dwidget.cpp
    comthread.h comthread.cpp
    comportmanager.h comportmanager.cpp
    #sensorworker.h sensorworker.cpp

)

# 4) Link against the Qt libraries (use the keyword form exactly once):
target_link_libraries(QtAlp
    PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::WebSockets
        Qt6::Sql
        Qt6::Widgets
        Qt6::Charts
        Qt6::SerialPort
        Qt6::DataVisualization
        OpenGL::GL
)

# 5) If you need moc/uic for Qt (we’re just using QCoreApplication, no widgets),
#    this is enough.  Otherwise enable AUTOMOC/AUTOUIC as needed:
set_target_properties(QtAlp PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
)




