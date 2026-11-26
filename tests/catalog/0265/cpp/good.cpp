#include <chrono>
#include <string>

using std::string;

string greet()
{
    return "hello";
}

std::chrono::seconds timeout()
{
    using namespace std::chrono_literals;
    return 5s;
}
