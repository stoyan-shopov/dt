#include <stdio.h>
#include <stdbool.h>

int main(void)
{
int c;
bool line_started;

	fputs("\"", stdout);
	while ((c = fgetc(stdin)) != EOF) switch(c)
	{
		case '\n': fputs("\\n\"\n\"", stdout); break;
		case '\\': fputs("\\\\", stdout); break;
		case '"': fputs("\\\"", stdout); break;
		default: fputc(c, stdout); break;
	}
	fputs("\"\n", stdout);
	return 0;
}

