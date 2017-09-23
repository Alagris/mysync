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

const bool IS_VERBOSE=true;
bool IS_DRY_RUN=true;
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
//        return reinterpret_cast<direntWrapper::direntWrap*>(readdir64(dirp));
        return readdir64(dirp);
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
        if(IS_DRY_RUN)return count;
        return sendfile(out_fd,  in_fd, offset, count);
    }

    template<> const ssize_t  sendfileWrapperTemplate<8>(int out_fd, int in_fd, off_t *offset, size_t count)
    {
        if(IS_DRY_RUN)return count;
        return sendfile64(out_fd,  in_fd, offset, count);
    }
    inline const ssize_t  sendfileWrapper(int out_fd, int in_fd, off_t *offset, size_t count) {
        if(IS_DRY_RUN)return true;
        return sendfileWrapperTemplate<sizeof(size_t)>( out_fd,  in_fd, offset, count);
    }

    inline const bool copyFileTimeData(const int toFd,const struct stat & fromInfo) {
        if(IS_DRY_RUN)return true;
        const struct timespec time[2]= {fromInfo.st_atim,fromInfo.st_mtim};
        return futimens(toFd,time)==0;
    }
    inline const bool copyFileMetadata(const int fromFd,const int toFd,const struct stat & fromInfo) {
        if(IS_DRY_RUN)return true;
        return (fchmod(toFd,fromInfo.st_mode)==0) && copyFileTimeData(toFd,fromInfo);
    }
    inline const bool copyDirTimeData(const char * const toDirPath,const struct stat & fromInfo) {
        if(IS_DRY_RUN)return true;
        timeval atim;
        timeval mtim;
        TIMESPEC_TO_TIMEVAL(&atim,&fromInfo.st_atim);
        TIMESPEC_TO_TIMEVAL(&mtim,&fromInfo.st_mtim);
        const struct timeval time[2]= {atim,mtim};
        return utimes(toDirPath,time)==0;
    }
    inline const bool copyDirMetadata(const char * const toDirPath,const struct stat & fromInfo) {
        if(IS_DRY_RUN)return true;
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
  public:
    File(const unsigned int len):m_len(len),m_arr(new char[m_len]),infoCollected(INFO_NOT_COLLECTED) {
    }
    File(const char *const fileName):File(strlen(fileName)+1) {
        strcpy(m_arr,fileName);
        collectInfo();
    }
    File(const File & parent,const char *const fileName,const unsigned int fileLen):
        File((const unsigned int)(parent.getLen()+1+fileLen+1))
    {
        fillPath(parent,fileName,fileLen);
        collectInfo();
    }
    ~File() {
        delete[] m_arr;
    }
    inline const unsigned int getLen()const {
        return m_len-1;
    }
    inline const unsigned int getLenWithNull()const {
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
        return S_ISDIR(info.st_mode);
//        return checkTypeInfo(S_IFDIR);
    }
    inline const bool isFile() const {
        return S_ISREG(info.st_mode);//checkTypeInfo(S_IFREG);
    }
    inline const bool exists() const {
        return hasInfoAvailable();
    }
    inline const off_t getSize()const {
        return info.st_size;
    }
    inline const bool isNewerThan(const File & other) const {
        return info.st_mtim.tv_sec>other.info.st_mtim.tv_sec||info.st_size!=other.info.st_size;
    }
    const bool mkdir(const mode_t mode=0) const {
        if(IS_DRY_RUN)return true;
        return ::mkdir(get(),mode)==0;
    }

    const bool copyDirTo(const File & destination)const {
        if(destination.exists()) {
            if(isNewerThan(destination)) {
                log("Update dir:",destination,false);
//                if(IS_DEBUG&& !LOG_ONLY_CHANGES)std::cout<<<<destination.get()<<"\n";
                copying::copyDirMetadata(destination.get(),info);
                return true;
            }
//            if(IS_DEBUG&& !LOG_ONLY_CHANGES)std::cout<<"Skipping dir:"<<destination.get()<<"\n";
            log("Skipping dir:",destination,false);
            return true;
        } else {
//            if(IS_DEBUG)std::cout<<"Mkdir:"<<destination.get()<<"\n";
            log("Mkdir:",destination,true);
            const bool out=destination.mkdir(info.st_mode);
            if(out) {

            }
            return out;
        }
    }
    const bool copyFileTo(const File & destination) const {
        int toFlags=O_WRONLY;
        if(destination.exists()) {
            if(destination.isFile()) {
                if(!isNewerThan(destination)) {
//                    if(IS_DEBUG&& !LOG_ONLY_CHANGES)std::cout<<"Skipping:"<<destination.get()<<"\n";
                    log("Skipping:",destination,false);
                    return true;
                }
            } else {
//                if(IS_DEBUG)std::cout<<"Already exists but not a file! "<<destination.get()<<"\n";
                err("Already exists but not a file!",destination);
                return false;
            }
        } else {
//            if(IS_DEBUG)std::cout<<"Creating:"<<destination.get()<<"\n";
            log("Creating:",destination,true);
            toFlags|=O_CREAT;
        }
        if(IS_DRY_RUN)return true;
        int fromFd=opening::openWrapper(get(),O_RDONLY);
        int toFd=opening::openWrapper(destination.get(),toFlags);
//        if(destination.isFile()){
//            fallocate(toFd,0,0,getSize());
//        }
        const ssize_t copied= copying::sendfileWrapper(toFd,fromFd,0,getSize());
        copying::copyFileMetadata(fromFd,toFd,info);
        close(toFd);
        close(fromFd);
        return copied==getSize();
    }
    inline const bool rm() {
//        if(IS_DEBUG)std::cout<<"Deleting: "<<get()<<"\n";
        log("Deleting: ",get(),true);
        if(IS_DRY_RUN)return true;
        return unlink(get())==0;
    }
    inline const bool rmdir() {
//        if(IS_DEBUG)std::cout<<"Deleting dir: "<<get()<<"\n";
        log("Deleting dir: ",get(),true);
        if(IS_DRY_RUN)return true;
        return ::rmdir(get())==0;
    }
    inline void collectInfo() {
        if(stat( get(), &info ) ==0) {
            infoCollected=INFO_COLLECTED_SUCCESSFULLY;
        } else {
            infoCollected=INFO_COLLECTING_FAILED;
        }
    }

  private:


    const bool hasInfoAvailable()const {
        return infoCollected == INFO_COLLECTED_SUCCESSFULLY;
    }
    void fillPath(const File & parent,const char *const fileName,const unsigned int fileLen) {
        unsigned int i=0;
        for(; i<parent.getLen(); i++) {
            m_arr[i]=parent[i];
        }
        m_arr[i++]='/';
        for(unsigned int j=0; j<fileLen; j++,i++) {

            m_arr[i]=fileName[j];
        }
        m_arr[i++]='\0';
    }
    const unsigned int m_len;
    char * const m_arr;
    struct stat info;
    uint8_t infoCollected;
    static const uint8_t INFO_NOT_COLLECTED=0;
    static const uint8_t INFO_COLLECTED_SUCCESSFULLY=1;
    static const uint8_t INFO_COLLECTING_FAILED=2;
};
void log(const char * const msg,const File & f,const bool isModificationLog) {
    if(IS_VERBOSE) {
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
            err("Error opening ",dir.get());
        } else {
            while (d.readNext()) {
                if(d.isDotOrDoubleDot())continue;
                const char * const name=d.get()->d_name;
                File newDir(dir,name,strlen(name));
                if(newDir.isDir()) {
                    rmdirs(newDir);
                    newDir.rmdir();
                } else if(newDir.isFile()) {
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
        err("Error opening ",to.get());
    } else {
        while (d.readNext()) {
            if(d.isDotOrDoubleDot())continue;
            const char * const name=d.get()->d_name;
            const unsigned int len=strlen(name);
            File newFrom(from,name,len);
            File newTo(to,name,len);
            if((newTo.exists() && !newFrom.exists())) {
                if(newTo.isDir()) {
                    if(IS_DRY_RUN) {
                        log("Deleting dir: ",newTo.get(),true);
//                        std::cout<<"Deleting dir: "<<newTo.get()<<"\n";
                        continue;
                    }
                    rmdirs(newTo);
                } else if(newTo.isFile()) {
                    newTo.rm();
                }
            } else {
                if(newFrom.isDir()) {
                    recursivePhase2(newFrom,newTo);
                }
            }

        }
    }

}

/***Copy everything FROM one directory TO another and update if modification date indicates so*/
void recursivePhase1(const File & from,const File & to) {
    Dir d(from.get());
    if(!d.isOpen()) {
//        std::cout << "Error opening " << from.get() << "\n";
        err("Error opening ",from.get());
    } else {
        while (d.readNext()) {
            if(d.isDotOrDoubleDot())continue;
            const char * const name=d.get()->d_name;
            const unsigned int len=strlen(name);
            File newFrom(from,name,len);
            File newTo(to,name,len);
            if(newFrom.isDir()) {
                newFrom.copyDirTo(newTo);
                if(IS_DRY_RUN) {
                    if(!newTo.exists())continue;
                }
                recursivePhase1(newFrom,newTo);
            } else if(newFrom.isFile()) {
                newFrom.copyFileTo(newTo);
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

int main(int argc,  char*argv[])
{
//    argc=3;
//    const char * argv[]= {"1","/my/garbage/from/","/my/garbage/to/"};


    boost::filesystem::path src;
    boost::filesystem::path dest;

    if(!isValid(argc,argv,src,dest)) {
        printf("This is a simple tool for files synchronization. Usage:\n"
               "mysync <phase> <source> <destination> (!)\n"
               "Where:\n"
               "<phase> is either 1 or 2. During phase 1 all files and folders are"
               "recursively copied from source directory to destination directory.\n"
               "During phase 2 program deletes all files and folders that are"
               "present in destination BUT NOT in source\n"
               "<source>  is the directory from which all files will be copied to destination."
               "Source directly itself won't be copied!\n"
               "<destination> is the directory that will receive all files from source.\n"
               "(!) optional parameter. By default program executes dry-run. If you add exclamation mark '!'"
               " as the fourth parameter, only then program will actually copy/delete files."
               "Notice that during dry-run, the output will show all directories and files that will get changed (copied or deleted)"
               "but once a directory is marked as 'changed' recursion will skip it. Only after adding '!' all directory"
               " contents will be printed. Directories and files that are not modified are never listed, no matter if dry-run or not." );
        return 0;
    }
    if(argc>=5) {
        if(argv[4][0]=='!'&&argv[4][1]=='\0') {
            IS_DRY_RUN=false;
        }
    }
    const int phase=static_cast<char>(argv[1][0]-'0');
    if(IS_DRY_RUN) {
        printf("DRY_RUN ");
    } else {
        printf("REALLY ");
    }
    if(phase==1) {
        printf("COPYING\n");
        recursivePhase1(src.c_str(),dest.c_str());
    } else if(phase==2) {
        printf("DELETING\n");
        recursivePhase2(src.c_str(),dest.c_str());
    } else {
        fprintf(stderr,"Unkown phase number!\n");
    }
    return 0;
}
