# Headless unit tests for the pure logic (no GPU device required at runtime).
add_executable(test_geometry test_geometry.cpp)
target_link_libraries(test_geometry PRIVATE book_core)
add_test(NAME geometry COMMAND test_geometry)
