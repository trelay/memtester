#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

int main()
{
	char logname[10];
	sprintf( logname,"test%d.log", 5);
	printf("name:%s\n",logname);


	FILE *f;
	f = fopen(logname, "a+");
	fprintf(f, "Im logging somethig ..\n");

}
