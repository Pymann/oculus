using callback = int (*)(int);

callback make()
{
    return +[](int value) { return value * 2; };   // lambda to function pointer
}
