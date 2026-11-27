#include <string>
#include <vector>

std::vector<std::string> split(const std::string &text);
std::string load(const char *name);

int count_words()
{
    int words = 0;
    for (const std::string &word : split(load("input.txt"))) {
        words += word.empty() ? 0 : 1;
    }
    return words;
}
