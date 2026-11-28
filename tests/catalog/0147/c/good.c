#define DOUBLE(x) ((x) * 2)
#define CALL(f, x) f(x)
#define NAME(x) #x
#define DECLARE(type, name) type name

int twice(int value)
{
    DECLARE(int, result) = DOUBLE(value + 1);
    (void)NAME(value);
    return CALL(DOUBLE, result);
}
