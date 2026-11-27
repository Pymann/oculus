double integrate(int steps)
{
    double position = 0.0;
    for (int index = 0; index < steps; index++) {
        position += 0.5;   /* changes each iteration but does not control the loop */
    }
    return position;
}
