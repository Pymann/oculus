int limit = 10;

int clamp(int value)
{
    int limit = 5;
    return value > limit ? limit : value;
}
