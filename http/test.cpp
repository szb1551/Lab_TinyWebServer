#include <stdio.h>
#include <iostream>

using namespace std;
int main()
{
    const char *p = "wwp";
    for (int i = 0; i < 5; i++)
    {
        cout << *(p + i) << endl;
        if (*(p + i) == '\0')
            cout << 1 << endl;
    }
}