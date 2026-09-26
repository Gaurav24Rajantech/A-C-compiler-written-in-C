int main() {
    int arr[4];
    arr[0] = 10; arr[1] = 20; arr[2] = 30; arr[3] = 40;
    int *p;
    p = arr;
    printf("%d\n", *p);
    p = p + 1;
    printf("%d\n", *p);
    p = p + 2;
    printf("%d\n", *p);

    int *q;
    q = &arr[3];
    printf("diff = %d\n", q - arr);

    return 0;
}
