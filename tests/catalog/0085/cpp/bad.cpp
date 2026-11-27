void clear(const int &value)
{
    int &writable = const_cast<int &>(value);
    writable = 0;
}
