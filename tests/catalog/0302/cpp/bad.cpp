// expect-compile-error
void run(int mode)
{
    if (mode == 0) {
        goto inside;
    }
    try {
    inside:
        mode++;
    } catch (...) {
    }
}
