
#include <sys/stat.h>

static char * read_whole_file(const char *name,OO_SINTa * pLength);


/*

read_whole_file : helper to read a file with stdio

*/

static char * read_whole_file(const char *name,OO_SINTa * pLength)
{
    #ifdef _MSC_VER
    struct __stat64 st;
    if ( _stat64(name,&st) != 0 ) return NULL;

    #else
    // stat64 and all that in posix is deprecated, just use stat :
    struct stat st;
    if ( stat(name,&st) != 0 ) return NULL;
    #endif
    
    OO_SINTa length = (OO_SINTa)st.st_size;
    if ( (OO_S64)length != (OO_S64)st.st_size ) return NULL;
    
    if ( length <= 0 ) { return NULL; }
    
    #ifdef _MSC_VER
    FILE * fp;
    errno_t err = fopen_s(&fp, name, "rb");
    if (err != 0)
        return NULL;
    #else
    FILE * fp = fopen(name,"rb");
    if ( ! fp )
        return NULL;
    #endif
    
    *pLength = length;  
        
    char * data = (char *) malloc( (size_t)length );

    if ( data )
    {
        fread(data,1,(size_t)length,fp);
    }
        
    fclose(fp);
    
    return data;
}

