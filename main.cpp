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
#include <errno.h>
bool isVerbose=true;
bool isDryRun=true;
bool forceSymlinks=false;
const bool LOG_ONLY_CHANGES=true;
const bool LOG_PATHS_ONLY=true;

void logImpl(FILE * const dest,const char * const msg,const char * const path,const unsigned int pathLen) {
//    const unsigned int msgLen=strlen(msg);
    if(LOG_PATHS_ONLY) {
        fprintf(dest,"%s\n",path);
    } else {
        fprintf(dest,"%s %s\n",msg,path);
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
    namespace file {
        inline const bool copyFileTimeData(const int toFd,const ::stat_reading::statWrap::statW & fromInfo) {
            if(isDryRun)return true;
            const struct timespec time[2]= {fromInfo.st_atim,fromInfo.st_mtim};
            return futimens(toFd,time)==0;
        }
        inline const bool copyFilePermissions(const int toFd,const ::stat_reading::statWrap::statW & fromInfo) {
            if(isDryRun)return true;
            return fchmod(toFd,fromInfo.st_mode)==0;
        }
        inline const bool copyFileOwnership(const int toFd,const ::stat_reading::statWrap::statW & fromInfo) {
            if(isDryRun)return true;
            return fchown(toFd,fromInfo.st_uid,fromInfo.st_gid)==0;
        }
    }
    namespace dir {
        inline const bool copyDirTimeData(const char * const toDirPath,const ::stat_reading::statWrap::statW & fromInfo) {
            if(isDryRun)return true;
            timeval atim;
            timeval mtim;
            TIMESPEC_TO_TIMEVAL(&atim,&fromInfo.st_atim);
            TIMESPEC_TO_TIMEVAL(&mtim,&fromInfo.st_mtim);
            const struct timeval time[2]= {atim,mtim};
            return utimes(toDirPath,time)==0;
        }
        inline const bool copyDirOwnership(const char * const toDirPath,const ::stat_reading::statWrap::statW & fromInfo) {
            if(isDryRun)return true;
            return chown(toDirPath,fromInfo.st_uid,fromInfo.st_gid)==0;
        }
        inline const bool copyDirPermissions(const char * const toDirPath,const ::stat_reading::statWrap::statW & fromInfo) {
            if(isDryRun)return true;
            return chmod(toDirPath,fromInfo.st_mode)==0;
        }
    }
    namespace symlnk {
        inline const bool copySymlinkTimeData(const char * const toPath,const ::stat_reading::statWrap::statW & fromInfo) {
            if(isDryRun)return true;
            timeval atim;
            timeval mtim;
            TIMESPEC_TO_TIMEVAL(&atim,&fromInfo.st_atim);
            TIMESPEC_TO_TIMEVAL(&mtim,&fromInfo.st_mtim);
            const struct timeval time[2]= {atim,mtim};
            return lutimes(toPath,time)==0;
        }

        inline const bool copySymlinkOwnership(const char * const toPath,const ::stat_reading::statWrap::statW & fromInfo) {
            if(isDryRun)return true;
            return lchown(toPath,fromInfo.st_uid,fromInfo.st_gid)==0;
        }
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
void readlinkNullTerminated(const char * const file,char * const targetBuf,const unsigned int targetBufSize) {
    const ssize_t linkLen=readlink(file,targetBuf,targetBufSize);
    if(linkLen<targetBufSize)targetBuf[linkLen]='\0';
}
const bool strcmpLimited(const char * const s1,const char * const s2,const unsigned int limit) {
    for(unsigned int i=0; i<limit; i++) {
        if(s1[i]!=s2[i])return false;
        if(s1[i]=='\0')return true;
    }
    return true;
}
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
            if(!destination.isDir()) {
                err("Already exists but not a directory!",destination);
                return false;
            }
            bool updated=false;
            if(!compareEqualPermissions(destination)) {
                updated=true;
                copyDirPermissionsTo(destination);
            }
            if(!compareEqualOwnership(destination)) {
                updated=true;
                copyDirOwnershipTo(destination);
            }
            if(isNewerThan(destination)) {
                updated=true;
                copyDirTimeDataTo(destination);
            }
            if(updated) {
                log("Update dir:",destination,false);
            } else {
                log("Skipping dir:",destination,false);
            }
            return true;
        } else {
            const bool out=destination.mkdir(m_info.st_mode);
            if(out) {
                copyDirPermissionsTo(destination);
                copyDirOwnershipTo(destination);
                copyDirTimeDataTo(destination);
                return true;
            }
            err("Copying failed!",*this);
            return false;
        }
    }
    const bool copyFileTo(const File & destination) const {
        int toFlags=O_WRONLY;
        if(destination.exists()) {
            if(!destination.isFile()) {
                err("Already exists but not a file!",destination);
                return false;
            }
            if(isNewerThan(destination)) {
                log("Updating:",destination,true);
            } else {
                bool updated=false;
                if(!compareEqualPermissions(destination)) {
                    updated=true;
                    //Dir functions work also for files.
                    //The distinction is only because we usually
                    //access files with file descriptors and
                    //directories with paths only
                    copyDirPermissionsTo(destination);
                }
                if(!compareEqualOwnership(destination)) {
                    updated=true;
                    copyDirOwnershipTo(destination);
                }
                if(updated){
                    log("Updating:",destination,false);
                }else{
                    log("Skipping:",destination,false);
                }
                return true;
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
        copyFilePermissionsTo(destination,toFd);
        copyFileOwnershipTo(destination,toFd);
        copyFileTimeDataTo(destination,toFd);
        close(toFd);
        close(fromFd);
        if(copied==getSize())return true;
        err("Copying failed!",*this);
        return false;
    }
    inline const bool rm() const {
        log("Deleting: ",*this,true);
        if(isDryRun)return true;
        if(unlink(get())==0)return true;
        err("Deleting failed!",*this);
        return false;
    }
    inline const bool rmdir() const {
        log("Deleting dir: ",*this,true);
        if(isDryRun)return true;
        if(::rmdir(get())==0)return true;
        err("Deleting failed!",*this);
        return false;
    }
    inline const bool symlinkTo(const char * const target)const {
        log("Symlinking: ",*this,true);
        if(isDryRun)return true;
        if(symlink(target,get())==0)return true;
        err("Symlinking failed!",*this);
        return false;
    }
    const bool copySymlinkTo(const File & newTo) const {
        char target[PATH_MAX];
        readlinkNullTerminated(get(),target,PATH_MAX);
        if(newTo.exists()) {
            if(newTo.isLink()) {
                char existingTarget[PATH_MAX];
                readlinkNullTerminated(newTo.get(),existingTarget,PATH_MAX);
                if(strcmpLimited(target,existingTarget,PATH_MAX)) {
                    //symlink already exists and points to the same target
                    copySymlinkOwnershipTo(newTo);
                    copySymlinkTimeDataTo(newTo);
                    return true;
                } else {
                    //symlink already exists and points to different place
                    newTo.rm();
                    return copySymlinkTo(newTo,target);
                }
            } else {
                err("Already exists but not a symlink!",newTo);
                return false;
            }
        }
        return copySymlinkTo(newTo,target);
    }
    inline void collectInfo() {
        if(stat_reading::statWrapper( get(), &m_info ) ==0) {
            m_infoCollected=INFO_COLLECTED_SUCCESSFULLY;
        } else {
            m_infoCollected=INFO_COLLECTING_FAILED;
        }
    }

    const bool compareEqualOwnership(const File & other) const {
        return other.m_info.st_gid==m_info.st_gid&&other.m_info.st_uid==m_info.st_uid;
    }
    const bool compareEqualPermissions(const File & other) const{
        const unsigned int PERMISSIONS_MASK=07777;
        return (other.m_info.st_mode&PERMISSIONS_MASK)==(m_info.st_mode&PERMISSIONS_MASK);
    }
  private:
    inline const bool copyDirTimeDataTo(const File & destination)const {
        if(copying::dir::copyDirTimeData(destination.get(),m_info))return true;
        err("Copying timestamp failed!",destination);
        return false;
    }
    inline const bool copyDirOwnershipTo(const File & destination)const {
        if(copying::dir::copyDirOwnership(destination.get(),m_info))return true;
        err("Copying ownership failed!",destination);
        return false;
    }
    inline const bool copyDirPermissionsTo(const File & destination)const {
        if(copying::dir::copyDirPermissions(destination.get(),m_info))return true;
        err("Copying permissions failed!",destination);
        return false;
    }
    inline const bool copyFileTimeDataTo(const File & destination,const int toFd)const {
        if(copying::file::copyFileTimeData(toFd,m_info)) return true;
        err("Copying timestamp failed!",destination);
        return false;
    }
    inline const bool copyFileOwnershipTo(const File & destination,const int toFd)const {
        if(compareEqualOwnership(destination)) return true;
        if(copying::file::copyFileOwnership(toFd,m_info))return true;
        err("Copying ownership failed!",destination);
        return false;
    }
    inline const bool copyFilePermissionsTo(const File & destination,const int toFd)const {
        if(compareEqualPermissions(destination)) return true;
        if(copying::file::copyFilePermissions(toFd,m_info))return true;
        err("Copying permissions failed!",destination);
        return false;
    }
    inline const bool copySymlinkTimeDataTo(const File & destination)const {
        if(destination.m_info.st_mtim.tv_sec==m_info.st_mtim.tv_sec)return true;
        if(copying::symlnk::copySymlinkTimeData(destination.get(),m_info))return true;
        err("Copying timestamp failed!",destination);
        return false;
    }
    inline const bool copySymlinkOwnershipTo(const File & destination)const {
        if(compareEqualOwnership(destination)) return true;
        if(copying::symlnk::copySymlinkOwnership(destination.get(),m_info))return true;
        err("Copying ownership failed!",destination);
        return false;
    }
    const bool copySymlinkTo(const File & newTo,const char *const target) const {
        if(newTo.symlinkTo(target)) {
            copySymlinkOwnershipTo(newTo);
            copySymlinkTimeDataTo(newTo);
            return true;
        }
        return false;
    }
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
    fprintf(stderr,"ERROR: %s %s\n",msg,f.get());
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
                newFrom.copySymlinkTo(newTo);
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
    } else if(arg[0]=='F') {
        forceSymlinks=true;
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
//    File tmpF("/mnt/backup/my/src/Python/Qt_GUIpy/YoutubeDownloader.py");
//    boost::filesystem::path tmpPath=boost::filesystem::canonical(tmpF.get());
//    printf("%d %d %d %d %s %s\n",tmpF.isDir(),tmpF.isFile(),tmpF.exists(),tmpF.isLink(),tmpF.get(), tmpPath.c_str());
//    return 0;

    boost::filesystem::path src;
    boost::filesystem::path dest;

    if(!isValid(argc,argv,src,dest)) {
        printf("This is a simple tool for files synchronization. Usage:\n"
               "mysync <phase> <source> <destination> (AA) (F)\n"
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
               "\n"
               "Be careful when using mysync together with sudo!\n"
              );
        return 0;
    }

    if(argc>=5) {
        setOptionalParameters(argv[4]);
        if(argc>=6) {
            setOptionalParameters(argv[5]);
        }
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
        fprintf(stderr,"Phase 3 not implemented yet!\n");
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




