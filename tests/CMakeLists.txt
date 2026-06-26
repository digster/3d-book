# Headless unit tests for the pure logic (no GPU device required at runtime):
# the box generator, camera math, placement, and the OBJ/glTF model loaders.
add_executable(test_geometry test_geometry.cpp)
target_link_libraries(test_geometry PRIVATE book_core)

# Let the loader tests find their fixtures (tests/assets/) regardless of CWD.
target_compile_definitions(test_geometry PRIVATE
    TEST_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")

add_test(NAME geometry COMMAND test_geometry)
