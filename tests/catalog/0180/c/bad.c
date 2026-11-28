static int spare_counter;
static int used_counter;

int bump(void)
{
    return ++used_counter;
}
