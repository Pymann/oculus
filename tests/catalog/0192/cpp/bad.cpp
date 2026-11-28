[[noreturn]] void stop(bool really)
{
    if (really) {
        throw 1;
    }
}
