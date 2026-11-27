int list_directory(const char *path);

int list_files(void)
{
    return list_directory(".");
}
