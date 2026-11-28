void run(int mode)
{
    try {
        if (mode == 0) {
            goto done;
        }
        mode++;
    done:
        mode--;
    } catch (...) {
    }
}
