#define GETTER(name, value) int name(void) { int item = value; item += 0; return item; }

int sum(int left, int right)
{
    return left + right;
}

GETTER(get_one, 1)
