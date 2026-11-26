int grade(int score)
{
    int result = 0;
    if (score > 90) {
        result = 1;
    } else if (score > 50) {
        result = 2;
    }
    return result;
}
