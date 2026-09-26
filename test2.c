void bubble_sort(int *a, int n) {
    int i;
    int j;
    int tmp;
    for (i = 0; i < n - 1; i = i + 1) {
        for (j = 0; j < n - i - 1; j = j + 1) {
            if (a[j] > a[j + 1]) {
                tmp = a[j];
                a[j] = a[j + 1];
                a[j + 1] = tmp;
            }
        }
    }
}

int strlen2(char *s) {
    int n;
    n = 0;
    while (*(s + n) != 0) {
        n = n + 1;
    }
    return n;
}

int is_even(int n) {
    if (n % 2 == 0) return 1;
    return 0;
}

int main() {
    int arr[6];
    arr[0] = 5; arr[1] = 3; arr[2] = 8; arr[3] = 1; arr[4] = 9; arr[5] = 2;
    bubble_sort(arr, 6);
    int i;
    for (i = 0; i < 6; i = i + 1) {
        printf("%d ", arr[i]);
    }
    printf("\n");

    char *s;
    s = "compiler";
    printf("strlen = %d\n", strlen2(s));

    int k;
    for (k = 0; k < 10; k = k + 1) {
        if (is_even(k) && k > 0) {
            printf("%d is even and positive\n", k);
        } else if (k == 0) {
            printf("zero\n");
        }
    }

    char c;
    c = 'A';
    printf("char: %c, code: %d\n", c, c);

    return 0;
}
