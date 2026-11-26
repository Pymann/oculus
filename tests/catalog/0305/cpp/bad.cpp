void rethrow()
{
    throw;
}

void run()
{
    try {
        rethrow();
    } catch (...) {
    }
}
