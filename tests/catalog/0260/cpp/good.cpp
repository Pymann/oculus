#include <string>
#include <utility>

void consume(std::string text);
void fill(int &target);

int area(int width)
{
    const int height = 4;
    int filled = 0;
    fill(filled);                 // modified through a reference
    std::string name = "x";
    consume(std::move(name));     // moved from
    return width * height + filled;
}
