#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <boost/filesystem.hpp>
#include <cstring>
#include <string>
#include <dirent.h>
#include <time.h>
#include <utime.h>
#include <stdio.h>

bool isVerbose=true;
bool isDryRun=true;
const bool LOG_ONLY_CHANGES=true;
const bool LOG_PATHS_ONLY=true;
void logImpl(FILE * const dest,const char * const msg,const char * const path,const unsigned int pathLen) {
//    const unsigned int msgLen=strlen(msg);
    if(LOG_PATHS_ONLY) {
        fprintf(dest,"%s\n",path);
    } else {
        fprintf(dest,"%s%s\n",msg,path);
    }
}
class File;
void log(const char * const msg,const File & f,const bool isModificationLog);
void err(const char * const msg,const File & f);
namespace stat_reading {
    template<int> struct statWrapTemplate;

    template<> struct statWrapTemplate<4>
    {
        typedef struct stat statW;
    };

    template<> struct statWrapTemplate<8>
    {
        typedef struct stat64 statW;
    };
    typedef statWrapTemplate<sizeof(size_t)> statWrap;

    template<int...> int statWrapperTemplate(const char *__restrict file,statWrap::statW *__restrict buf);

    template<> int statWrapperTemplate<4>(const char *__restrict file,statWrap::statW *__restrict buf)
    {
        return lstat(file, reinterpret_cast<struct stat *__restrict>(buf));
    }

    template<> int statWrapperTemplate<8>(const char *__restrict file,statWrap::statW *__restrict buf)
    {
        return lstat64(file, reinterpret_cast<struct stat64 *__restrict>(buf));
    }
    // helper function just to hide clumsy syntax
    inline int statWrapper(const char *__restrict file,statWrap::statW *__restrict buf) {
        return statWrapperTemplate<sizeof(size_t)>( file, buf);
    }
}
namespace reading {
    //template metaprogramming that solves real life problem! Whoohoo!
    template<int> struct direntWrapperTemplate;

    template<> struct direntWrapperTemplate<4>
    {
        typedef dirent direntWrap;
    };

    template<> struct direntWrapperTemplate<8>
    {
        typedef dirent64 direntWrap;
    };
    typedef direntWrapperTemplate<sizeof(size_t)> direntWrapper;


    template<int...> direntWrapper::direntWrap *  readdirWrapperTemplate(DIR * dirp);

    template<> direntWrapper::direntWrap * readdirWrapperTemplate<4>(DIR * dirp)
    {
        return reinterpret_cast<direntWrapper::direntWrap*>(readdir(dirp));
    }

    template<> direntWrapper::direntWrap *  readdirWrapperTemplate<8>(DIR * dirp)
    {
        return reinterpret_cast<direntWrapper::direntWrap*>(readdir64(dirp));
    }
    // helper function just to hide clumsy syntax
    inline direntWrapper::direntWrap *  readdirWrapper(DIR * dirp) {
        return readdirWrapperTemplate<sizeof(size_t)>( dirp);
    }
}
namespace opening {
    template<int...> int openWrapperTemplate(const char *file, int oflag, ...);

    template<> int openWrapperTemplate<4>(const char *file, int oflag, ...)
    {
        return open(file, oflag);
    }

    template<> int openWrapperTemplate<8>(const char *file, int oflag, ...)
    {
        return open64(file, oflag);
    }
    // helper function just to hide clumsy syntax
    inline int openWrapper(const char *file, int oflag, ...) {
        return openWrapperTemplate<sizeof(size_t)>( file, oflag);
    }
}
namespace copying {
    template<int> const ssize_t  sendfileWrapperTemplate(int out_fd, int in_fd, off_t *offset, size_t count);

    template<> const ssize_t sendfileWrapperTemplate<4>(int out_fd, int in_fd, off_t *offset, size_t count)
    {
        if(isDryRun)return count;
        return sendfile(out_fd,  in_fd, offset, count);
    }

    template<> const ssize_t  sendfileWrapperTemplate<8>(int out_fd, int in_fd, off_t *offset, size_t count)
    {
        if(isDryRun)return count;
        return sendfile64(out_fd,  in_fd, offset, count);
    }
    inline const ssize_t  sendfileWrapper(int out_fd, int in_fd, off_t *offset, size_t count) {
        if(isDryRun)return true;
        return sendfileWrapperTemplate<sizeof(size_t)>( out_fd,  in_fd, offset, count);
    }

    inline const bool copyFileTimeData(const int toFd,const ::stat_reading::statWrap::statW & fromInfo) {
        if(isDryRun)return true;
        const struct timespec time[2]= {fromInfo.st_atim,fromInfo.st_mtim};
        return futimens(toFd,time)==0;
    }
    inline const bool copyFileMetadata(const int fromFd,const int toFd,const ::stat_reading::statWrap::statW & fromInfo) {
        if(isDryRun)return true;
        return (fchmod(toFd,fromInfo.st_mode)==0) && copyFileTimeData(toFd,fromInfo);
    }
    inline const bool copyDirTimeData(const char * const toDirPath,const ::stat_reading::statWrap::statW & fromInfo) {
        if(isDryRun)return true;
        timeval atim;
        timeval mtim;
        TIMESPEC_TO_TIMEVAL(&atim,&fromInfo.st_atim);
        TIMESPEC_TO_TIMEVAL(&mtim,&fromInfo.st_mtim);
        const struct timeval time[2]= {atim,mtim};
        return utimes(toDirPath,time)==0;
    }
    inline const bool copyDirMetadata(const char * const toDirPath,const ::stat_reading::statWrap::statW & fromInfo) {
        if(isDryRun)return true;
        return chmod(toDirPath,fromInfo.st_mode)==0 &&copyDirTimeData(toDirPath,fromInfo);
    }


}



class Dir {
  public:
    Dir(const char * const dir) :m_dp (opendir(dir)) {}
    ~Dir() {
        closedir(m_dp);
        m_dp=nullptr;
    }
    const bool isOpen()const {
        return m_dp!=nullptr;
    }
    const bool readNext() {
        m_next= reading::readdirWrapper(m_dp);
        return m_next!=nullptr;
    }
    const bool isDotOrDoubleDot()const {
        if(m_next->d_name[0]=='.') {
            if(
                (m_next->d_name[1]=='.'&&m_next->d_name[2]==0)
                ||m_next->d_name[1]=='\0'
            ) {
                return true;
            }
        }
        return false;
    }
    const reading::direntWrapper::direntWrap * const get()const {
        return m_next;
    }
  private:
    DIR *m_dp;
    reading::direntWrapper::direntWrap  * m_next;
};

class File {
  private:
    File(const unsigned int len,char *const arrayPtr,const unsigned int addedLen):
        m_len(len),m_arr(arrayPtr),m_addedLen(addedLen),m_infoCollected(INFO_NOT_COLLECTED) {
    }
  public:

    explicit File(char *const fileName):File(strlen(fileName),fileName,0) {
        collectInfo();
    }
    File(const File & parent,const char *const fileName,const unsigned int fileLen):
        File(parent.getLen()+computeAddedLen(fileLen),parent.m_arr,computeAddedLen(fileLen))
    {
        fillPath(parent.getLen(),fileName,fileLen);
        collectInfo();
    }
    File(const File & other):File(other.getLen(),other.m_arr,0) {}

    ~File() {
//        for(unsigned int i=getLen()-m_addedLen; i<getLen(); i++) {
//            m_arr[i]='\0';
//        }
        m_arr[getLen()-m_addedLen]='\0';
    }
    inline void reset(const char *const fileName,const unsigned int fileLen) {
        fillPath(getParentLen(),fileName,fileLen);
        m_len=getParentLen()+computeAddedLen(fileLen);
        m_addedLen=computeAddedLen(fileLen);
        collectInfo();
    }
    inline const unsigned int getLen()const {
        return m_len;
    }
    inline const char * const get()const {
        return m_arr;
    }
    inline char * const get() {
        return m_arr;
    }
    inline const char operator[](const unsigned int index)const {
        return m_arr[index];
    }
    inline char & operator[](const unsigned int index) {
        return m_arr[index];
    }
    inline const bool isDir() const {
        return S_ISDIR(m_info.st_mode);
    }
    inline const bool isFile() const {
        return S_ISREG(m_info.st_mode);
    }
    inline const bool isLink() const {
        return S_ISLNK(m_info.st_mode);
    }
    inline const bool exists() const {
        return hasInfoAvailable();
    }
    inline const off_t getSize()const {
        return m_info.st_size;
    }
    inline const bool isNewerThan(const File & other) const {
        return m_info.st_mtim.tv_sec>other.m_info.st_mtim.tv_sec||m_info.st_size!=other.m_info.st_size;
    }
    inline const bool mkdir(const mode_t mode=0) const {
        log("Mkdir:",*this,true);
        if(isDryRun)return true;
        return ::mkdir(get(),mode)==0;
    }

    const bool copyDirTo(const File & destination)const {
        if(destination.exists()) {
            if(isNewerThan(destination)) {
                log("Update dir:",destination,false);
                copying::copyDirMetadata(destination.get(),m_info);
                return true;
            }
            log("Skipping dir:",destination,false);
            return true;
        } else {
            const bool out=destination.mkdir(m_info.st_mode);
            if(out) {
                return copying::copyDirMetadata(destination.get(),m_info);
            }
            return false;
        }
    }
    const bool copyFileTo(const File & destination) const {
        int toFlags=O_WRONLY;
        if(destination.exists()) {
            if(destination.isFile()) {
                if(!isNewerThan(destination)) {
                    log("Skipping:",destination,false);
                    return true;
                }
            } else {
                err("Already exists but not a file!",destination);
                return false;
            }
        } else {
            log("Creating:",destination,true);
            toFlags|=O_CREAT;
        }
        if(isDryRun)return true;
        int fromFd=opening::openWrapper(get(),O_RDONLY);
        int toFd=opening::openWrapper(destination.get(),toFlags);
//        if(destination.isFile()){
//            fallocate(toFd,0,0,getSize());
//        }

        const ssize_t copied= copying::sendfileWrapper(toFd,fromFd,0,getSize());
        copying::copyFileMetadata(fromFd,toFd,m_info);
        close(toFd);
        close(fromFd);
        return copied==getSize();
    }
    inline const bool rm() {
        log("Deleting: ",*this,true);
        if(isDryRun)return true;
        return unlink(get())==0;
    }
    inline const bool rmdir() {
        log("Deleting dir: ",*this,true);
        if(isDryRun)return true;
        return ::rmdir(get())==0;
    }
    inline void collectInfo() {
        if(stat_reading::statWrapper( get(), &m_info ) ==0) {
            m_infoCollected=INFO_COLLECTED_SUCCESSFULLY;
        } else {
            m_infoCollected=INFO_COLLECTING_FAILED;
        }
    }

  private:
    constexpr const unsigned int computeAddedLen(const unsigned int fileLen)const {
        return fileLen+1;
    }
    const unsigned int getParentLen()const {
        return getLen()-m_addedLen;
    }

    const bool hasInfoAvailable()const {
        return m_infoCollected == INFO_COLLECTED_SUCCESSFULLY;
    }
    void fillPath(const unsigned int parentLen,const char *const fileName,const unsigned int fileLen) {
//        unsigned int i=0;
//        for(; i<parent.getLen(); i++) {
//            m_arr[i]=parent[i];
//        }
        unsigned int i=parentLen;
        m_arr[i++]='/';
        for(unsigned int j=0; j<fileLen; j++,i++) {
            m_arr[i]=fileName[j];
        }
        m_arr[i++]='\0';
    }
    unsigned int m_len;
    char * const m_arr;
    unsigned int m_addedLen;
    stat_reading::statWrap::statW m_info;
    uint8_t m_infoCollected;
    static const uint8_t INFO_NOT_COLLECTED=0;
    static const uint8_t INFO_COLLECTED_SUCCESSFULLY=1;
    static const uint8_t INFO_COLLECTING_FAILED=2;
};
void log(const char * const msg,const File & f,const bool isModificationLog) {
    if(isVerbose) {
        if(LOG_ONLY_CHANGES) {
            if(isModificationLog) {
                logImpl(stdout,msg,f.get(),f.getLen());
            }
        } else {
            logImpl(stdout,msg,f.get(),f.getLen());
        }
    }
}
void err(const char * const msg,const File & f) {
    logImpl(stderr,msg,f.get(),f.getLen());
}

/***Delete everything from TO  if it's not present in FROM.*/
void rmdirs(File & dir) {
    {
        Dir d(dir.get());
        if(!d.isOpen()) {
            err("Error opening ",dir);
        } else {
            File newDir(dir);
            while (d.readNext()) {
                if(d.isDotOrDoubleDot())continue;
                const char * const name=d.get()->d_name;
                newDir.reset(name,strlen(name));
                if(newDir.isDir()) {
                    rmdirs(newDir);
                } else if(newDir.isFile() || newDir.isLink()) {
                    newDir.rm();
                }
            }
        }
    }
    dir.rmdir();
}
/***Delete everything from TO  if it's not present in FROM.*/
void recursivePhase2(const File & from,const File & to) {
    Dir d(to.get());
    if(!d.isOpen()) {
        err("Error opening ",to);
    } else {
        File newFrom(from);
        File newTo(to);
        while (d.readNext()) {
            if(d.isDotOrDoubleDot())continue;
            const char * const name=d.get()->d_name;
            const unsigned int len=strlen(name);
            newFrom.reset(name,len);
            newTo.reset(name,len);
//            printf("%s %s\n",newFrom.get(),newTo.get());
            if((newTo.exists() && !newFrom.exists())) {
                if(newTo.isDir()) {
                    if(isDryRun) {
                        log("Deleting dir: ",newTo,true);
                        continue;
                    }
                    rmdirs(newTo);
                } else if(newTo.isFile() || newTo.isLink()) {
                    newTo.rm();
                }
            } else {
                if(newTo.isDir()) {
                    recursivePhase2(newFrom,newTo);
                }
            }

        }
    }

}
void recursivePhase1(const File & from,const File & to);
void recCpy1(const File & newFrom,const File & newTo) {
    if(newFrom.isDir()) {
        newFrom.copyDirTo(newTo);
        if(isDryRun) {
            if(!newTo.exists())return;
        }
        recursivePhase1(newFrom,newTo);
    } else if(newFrom.isFile()) {
        newFrom.copyFileTo(newTo);
    }
}
/***Copy everything FROM one directory TO another and update if modification date indicates so*/
void recursivePhase1(const File & from,const File & to) {

    Dir d(from.get());
    if(!d.isOpen()) {
        err("Error opening ",from);
    } else {
        File newFrom(from);
        File newTo(to);
        while (d.readNext()) {
            if(d.isDotOrDoubleDot())continue;
            const char * const name=d.get()->d_name;
            const unsigned int len=strlen(name);
            newFrom.reset(name,len);
            newTo.reset(name,len);

            if(newFrom.isLink()) {
                char copy[PATH_MAX];
                strcpy(copy,boost::filesystem::canonical(newFrom.get()).c_str());
                File resolvedFrom(copy);
                recCpy1(resolvedFrom,newTo);
            } else {
                recCpy1(newFrom,newTo);
            }
        }
    }

}

const bool isValid(const int argc,const char *const argv[],boost::filesystem::path & src,boost::filesystem::path & dest) {
    if(argc<4) {
        fprintf(stderr,"Incorrect command parameters!\n");
        return false;
    }
    if(argv[1][1]!='\0' ||argv[1][0]<'0'||argv[1][0]>'9') {
        fprintf(stderr,"Not a valid phase number!\n");
        return false;
    }
    src=boost::filesystem::canonical(argv[2]);
    dest=boost::filesystem::canonical(argv[3]);
    const char * source=src.c_str();
    const char * destination=dest.c_str();
    if(!boost::filesystem::is_directory(src)) {
        fprintf(stderr,"Not a directory: %s\n",source);
        return false;
    }
    if(!boost::filesystem::is_directory(dest)) {
        fprintf(stderr,"Not a directory: %s\n",destination);
        return false;
    }
    for(unsigned int i=0; i<500; i++) {
        if(source[i]!=destination[i]) {
            if(source[i]=='\0'||destination[i]=='\0') {
                if(source[i]=='/') {
                    fprintf(stderr,"Destination directory is ancestor of source!\n");
                    return false;
                }
                if(destination[i]=='/') {
                    fprintf(stderr,"Source directory is ancestor of destination!\n");
                    return false;
                }
            }
            return true;
        }
    }
    return true;
}
void setOptionalParameters(const char * const arg) {
    if(arg[0]=='A') {
        isDryRun=false;
        if(arg[1]=='A') {
            isVerbose=false;
        }
        return;
    } else if(arg[0]=='\0') {
        return;
    }

    fprintf(stderr,"Invalid optional parameter!\n");
}


int main(int argc,  char*argv[])
{
//    argc=3;
//    const char * argv[]= {"1","/my/garbage/from/","/my/garbage/to/"};
//    File f("/my/garbage/to/lnk");
//    boost::filesystem::path tmpPath=boost::filesystem::canonical(f.get());
//    printf("%d %d %d %d %s %s\n",f.isDir(),f.isFile(),f.exists(),f.isLink(),f.get(), tmpPath.c_str());
//    return 0;
    boost::filesystem::path src;
    boost::filesystem::path dest;

    if(!isValid(argc,argv,src,dest)) {
        printf("This is a simple tool for files synchronization. Usage:\n"
               "mysync <phase> <source> <destination> (AA)\n"
               "Where:\n"
               "<phase> is either 1, 2 or 3. During phase 1 all files and folders are "
               "recursively copied from source directory to destination directory.\n"
               "During phase 2 program deletes all files and folders that are "
               "present in destination BUT NOT in source. Phase 3 is just both phases 1 and 2 running simultaneously.\n"
               "<source>  is the directory from which all files will be copied to destination. "
               "Source directly itself won't be copied!\n"
               "<destination> is the directory that will receive all files from source.\n"
               "(A) optional parameter. By default program executes dry-run. If you add 'A' "
               "as the fourth parameter, only then program will actually copy/delete files."
               "Notice that during dry-run, the output will show all directories and files that will get changed (copied or deleted)"
               "but once a directory is marked as 'changed' recursion will skip it. Only after adding 'A' all directory "
               "contents will be printed. Directories and files that are not modified are never listed, no matter if dry-run or not.\n"
               "If you add second 'A' (so it's double 'AA') then all output (except errors) will be surpressed. "
               "This way you might achieve some extra performance gain.\n"
               "Important: notice that symbolic links are followed during copying and each time a deep copy is performed. "
               "The symlink itself is not copied but a new directory/file is created and named the same way as symlink. In case of directory, "
               "all contents of symlink are copied too. During deleting (phase 2) symlinks are not followed. If necessary then symlinks "
               "themselves are deleted, but their contents and referenced files stay intact.\n"
               "Hard links, due to their nature, are not differentianted from regular files/directories. If a hard link is encountered by program, "
               "then it will be treated just like any other file/directory.\n"
               "\n"
               "It is recommended to NOT use mysync together with sudo or any other overpriviliged accounts!\n"
              );
        return 0;
    }

    if(argc>=5) {
        setOptionalParameters(argv[4]);
    }

    const int phase=static_cast<char>(argv[1][0]-'0');
    if(isDryRun) {
        printf("DRY_RUN ");
    } else {
        printf("REALLY ");
    }
    char from[PATH_MAX];
    char to[PATH_MAX];
    strcpy(from,src.c_str());
    strcpy(to,dest.c_str());
    File f(from);
    File t(to);

    if(phase==1) {
        printf("COPYING\n");
        recursivePhase1(f,t);
    } else if(phase==2) {
        printf("DELETING\n");
        recursivePhase2(f,t);
    } else if(phase==3) {
        printf("DELETING AND COPYING\n");
//        recursivePhase2(src.c_str(),dest.c_str());
        fprintf(stderr,"Phase 3 not yet implemented!\n");
    } else {
        fprintf(stderr,"Unkown phase number!\n");
    }
    return 0;
}


//Benchmarks:
//COPYING (mysync ,output surpressed)
//real	0m4.444s
//user	0m0.013s
//sys	0m1.262s

//real	0m1.753s
//user	0m0.008s
//sys	0m1.116s

//real	0m1.866s
//user	0m0.004s
//sys	0m1.062s

//real	0m1.780s
//user	0m0.008s
//sys	0m1.072s

//COPYING (linux cp command)

//real	0m1.900s
//user	0m0.000s
//sys	0m1.254s

//real	0m1.879s
//user	0m0.011s
//sys	0m1.203s

//real	0m1.883s
//user	0m0.007s
//sys	0m1.256s

//reals	0m1.953s
//user	0m0.012s
//sys	0m1.282s

//DELETING (mysync ,output surpressed)

//real	0m0.210s
//user	0m0.004s
//sys	0m0.206s

//real	0m0.212s
//user	0m0.007s
//sys	0m0.204s

//real	0m3.047s
//user	0m0.004s
//sys	0m0.271s

//real	0m0.176s
//user	0m0.004s
//sys	0m0.172s

//DELETING (linux rm command)
//notice that rm deletes everything recursively in direcroy
//while mysync compares contents of 2 directories at once.
//Therefore the comparision is slightly unfair. Nonetheless
//mysync isn't that far behind. For example mysync sometimes
//outperforms rm in sys time.

//real	0m0.212s
//user	0m0.007s
//sys	0m0.205s

//real	0m0.184s
//user	0m0.000s
//sys	0m0.184s

//real	0m1.019s
//user	0m0.001s
//sys	0m0.219s

//real	0m1.977s
//user	0m0.000s
//sys	0m0.229s




