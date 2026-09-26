int fib(int n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}

int sum_array(int *a, int n) {
    int i;
    int s;
    s = 0;
    for (i = 0; i < n; i = i + 1) {
        s = s + a[i];
    }
    return s;
}

int main() {
    int arr[5];
    arr[0] = 1;
    arr[1] = 2;
    arr[2] = 3;
    arr[3] = 4;
    arr[4] = 5;

    int total;
    total = sum_array(arr, 5);

    printf("fib(10) = %d\n", fib(10));
    printf("sum = %d\n", total);

    char *msg;
    msg = "hello from scc\n";
    printf("%s", msg);

    int x;
    x = 7;
    int *p;
    p = &x;
    *p = 42;
    printf("x = %d\n", x);

    return 0;
}
