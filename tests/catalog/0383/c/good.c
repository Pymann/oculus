int grade(int value)
{
    int result = 0;
    if (value > 90) {
        result = 1;
    } else if (value > 80) {
        result = 2;
    } else if (value > 70) {
        if (value == 75) {
            result = 3;
        }
    } else {
        result = 4;
    }
    return result;
}
