#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
  if (argc != 2) {
    printf("Usage : argv[0] [ch#]\n");
    return 1;
  }

  FILE *fp = fopen("yaml/RegisterValue.yml","r");
  if (!fp) {
    fprintf(stderr, "cannot open RegisterValue.yml\n");
    return 1;
  }
  
  int chNext = atoi(argv[1]);
  if (!(chNext>=0 && chNext<=63)) {
    fprintf(stderr, "channel should be 0<=ch<=63\n");    
    return 1;
  }

  FILE *fp2 = fopen("yaml/RegisterValue_new.yml","w");
  if (!fp2) {
    fprintf(stderr, "cannot open RegisterValue_new.yml\n");
    return 1;
  }

  char buf[100];
  int ch;
  while (1) {
    if (fgets(buf, sizeof(buf), fp) == NULL)
      break;
    if (sscanf(buf, "High Gain Channel: %d", &ch) == 1) {
      fprintf(fp2, "High Gain Channel: %d\n", chNext);
    } else {
      fprintf(fp2, "%s", buf);
    }
  }

  fclose(fp);
  fclose(fp2);
}
