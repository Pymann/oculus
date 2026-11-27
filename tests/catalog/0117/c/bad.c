int label(int value)
{
    int result = 0;
    switch (value > 10) {
    case 0:
        result = 1;
        break;
    default:
        result = 2;
        break;
    }
    return result;
}
