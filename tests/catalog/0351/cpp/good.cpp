#include <array>

std::array<int, 16> samples{};

int main(int argc, char *argv[])
{
    return argc + (argv[0] != nullptr ? 0 : 1);
}
