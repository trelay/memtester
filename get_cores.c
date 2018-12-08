#include <stdio.h>
#include <sys/sysinfo.h>
#include <unistd.h>
int main(int argc, char *argv[])
{


    signed long long pages = sysconf(_SC_PHYS_PAGES);
    signed long long avpages = sysconf(_SC_AVPHYS_PAGES);
    signed long long pagesize_1 = sysconf(_SC_PAGESIZE);
    signed long long physsize = pages * pagesize_1;
    signed long long avphyssize = avpages * pagesize_1;
    signed long long ttcores = get_nprocs_conf();
    signed long long avcores = get_nprocs();
    
    signed long long pagespercore = avpages/avcores;

    printf("Has %d processors configured\n%d processors availables.\n\n", ttcores, avcores);
    printf("avpages: %lld,\n pagespercore:%lld\n\n", avpages,pagespercore);
    

    return 0;
    
}
