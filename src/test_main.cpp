#include "gtest/gtest.h"

int my_argc;
char** my_argv;

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    my_argc= argc;
    my_argv= argv;

    return RUN_ALL_TESTS();
}
