/* An identifier that merely contains "asm" is not assembly. */
static int plasma_level;

int read_plasma(void)
{
    return plasma_level;
}
