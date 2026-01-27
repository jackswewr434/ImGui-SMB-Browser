#ifndef BACKEND_H
#define BACKEND_H

#include <samba-4.0/libsmbclient.h>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>
#include <cstring>
#include <cerrno>
#include <functional>
#include <cstdint>
#include <fcntl.h>
#include <filesystem>
struct SMBFileInfo {
    std::string name;
    bool is_dir;
    uint64_t size;
};

// Global auth credentials used by the SMB auth callback
static std::string g_smb_username = "";
static std::string g_smb_password = "";

// Global auth callback (fixes deprecated lambda issue)
void auth_fn(const char *server, const char *share,
             char *workgroup, int wgmaxlen,
             char *username, int unmaxlen,
             char *password, int pwmaxlen) {
    if (!g_smb_username.empty()) {
        strncpy(username, g_smb_username.c_str(), unmaxlen - 1);
        username[unmaxlen - 1] = '\0';
    } else {
        strncpy(username, "guest", unmaxlen - 1);
        username[unmaxlen - 1] = '\0';
    }

    if (!g_smb_password.empty()) {
        strncpy(password, g_smb_password.c_str(), pwmaxlen - 1);
        password[pwmaxlen - 1] = '\0';
    } else {
        password[0] = '\0';
    }

    workgroup[0] = 0;
}

std::vector<SMBFileInfo> ListSMBFiles(const std::string& server, const std::string& share, 
                                    const std::string& path, const std::string& username, 
                                    const std::string& password) {
    std::vector<SMBFileInfo> files;
    
    // Set credentials for auth callback and initialize
    g_smb_username = username;
    g_smb_password = password;
    smbc_init(auth_fn, 1);  // Uses function pointer, not lambda
    
    std::string smb_url = "smb://" + server;
    if (!share.empty()) smb_url += "/" + share;
    if (!path.empty()) smb_url += "/" + path;
    

    int dh = smbc_opendir(smb_url.c_str());
    if (dh == -1) return files;
    
    struct smbc_dirent* dirent;
    while ((dirent = smbc_readdir(dh)) != NULL) { 
        if (strcmp(dirent->name, ".") == 0 || strcmp(dirent->name, "..") == 0) 
            continue;
            
        SMBFileInfo info;
        info.name = dirent->name;
        info.is_dir = (dirent->smbc_type == SMBC_DIR); 
        info.size = 0;  
        
        files.push_back(info);
    }
    
    smbc_closedir(dh);  
    return files;
}

bool DownloadFile(const std::string& server, const std::string& share, 
                  const std::string& path, const std::string& localFile,
                  const std::string& username, const std::string& password) {
    g_smb_username = username;
    g_smb_password = password;
    smbc_init(auth_fn, 1);
    
    std::string smb_url = "smb://" + server + "/" + share + "/" + path;
    
    // Returns int fd (NOT SMBCFILE*)
    int fd = smbc_open(smb_url.c_str(), O_RDONLY, 0);
    if (fd == -1) return false;
    
    std::ofstream out(localFile, std::ios::binary);
    if (!out.is_open()) {
        smbc_close(fd);
        return false;
    }
    
    char buffer[4096];
    ssize_t bytes;
    while ((bytes = smbc_read(fd, buffer, sizeof(buffer))) > 0) {
        out.write(buffer, bytes);
    }
    
    out.close();
    smbc_close(fd);
    return true;
}

bool UploadFile(const std::string& server, const std::string& share, 
                const std::string& remotePath, const std::string& localFile,
                const std::string& username, const std::string& password) {
    g_smb_username = username;
    g_smb_password = password;
    smbc_init(auth_fn, 1);
    
    std::string smb_url = "smb://" + server + "/" + share + "/" + remotePath;
    
    // Open LOCAL file for reading
    std::ifstream in(localFile, std::ios::binary);
    if (!in.is_open()) {
        printf("UploadFile: failed to open local file '%s'\n", localFile.c_str());
        return false;
    }

    // Open REMOTE file for writing (O_WRONLY | O_CREAT | O_TRUNC)
    int fd = smbc_open(smb_url.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd == -1) {
        printf("UploadFile: failed to open remote '%s'\n", smb_url.c_str());
        in.close();
        return false;
    }

    char buffer[4096];
    while (true) {
        in.read(buffer, sizeof(buffer));
        std::streamsize bytes = in.gcount();
        if (bytes > 0) {
            ssize_t written = smbc_write(fd, buffer, (size_t)bytes);
            if (written != bytes) {
                printf("UploadFile: write error (wrote %zd of %zd)\n", written, bytes);
                smbc_close(fd);
                in.close();
                return false;
            }
        }
        if (!in) break; // EOF or error after processing gcount
    }

    smbc_close(fd);
    in.close();
    printf("UploadFile: upload finished for local '%s' -> '%s'\n", localFile.c_str(), smb_url.c_str());
    return true;
}

// Upload with progress callback: calls progress_cb(bytes_written) after each chunk written.
bool UploadFileWithProgress(const std::string& server, const std::string& share,
                            const std::string& remotePath, const std::string& localFile,
                            const std::string& username, const std::string& password,
                            const std::function<void(ssize_t)>& progress_cb) {
    g_smb_username = username;
    g_smb_password = password;
    smbc_init(auth_fn, 1);

    std::string smb_url = "smb://" + server + "/" + share + "/" + remotePath;

    std::ifstream in(localFile, std::ios::binary);
    if (!in.is_open()) {
        printf("UploadFileWithProgress: failed to open local file '%s'\n", localFile.c_str());
        return false;
    }

    int fd = smbc_open(smb_url.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd == -1) {
        printf("UploadFileWithProgress: failed to open remote '%s'\n", smb_url.c_str());
        in.close();
        return false;
    }

    char buffer[8192];
    while (true) {
        in.read(buffer, sizeof(buffer));
        std::streamsize bytes = in.gcount();
        if (bytes > 0) {
            ssize_t written = smbc_write(fd, buffer, (size_t)bytes);
            if (written != bytes) {
                printf("UploadFileWithProgress: write error (wrote %zd of %zd)\n", written, bytes);
                smbc_close(fd);
                in.close();
                return false;
            }
            if (progress_cb) progress_cb((ssize_t)written);
        }
        if (!in) break;
    }

    smbc_close(fd);
    in.close();
    printf("UploadFileWithProgress: upload finished for local '%s' -> '%s'\n", localFile.c_str(), smb_url.c_str());
    return true;
}

// Delete remote file or directory. If `is_dir` is true, attempts rmdir.
bool DeleteFile(const std::string& server, const std::string& share,
                const std::string& remotePath, bool is_dir,
                const std::string& username, const std::string& password) {
    g_smb_username = username;
    g_smb_password = password;
    smbc_init(auth_fn, 1);

    std::string smb_url = "smb://" + server + "/" + share + "/" + remotePath;
    int res = -1;
    if (is_dir) {
        res = smbc_rmdir(smb_url.c_str());
    } else {
        res = smbc_unlink(smb_url.c_str());
    }
    if (res == -1) {
        printf("DeleteFile: failed to delete '%s' (errno=%d)\n", smb_url.c_str(), errno);
        return false;
    }
    printf("DeleteFile: deleted '%s'\n", smb_url.c_str());
    return true;
}

// Recursively delete a directory tree on the SMB share. If the target is not
// a directory, this will attempt to unlink it as a file.
bool DeleteRecursive(const std::string& server, const std::string& share,
                     const std::string& remotePath,
                     const std::string& username, const std::string& password) {
    g_smb_username = username;
    g_smb_password = password;
    smbc_init(auth_fn, 1);

    std::string smb_url = "smb://" + server + "/" + share;
    if (!remotePath.empty()) smb_url += "/" + remotePath;

    // Try opening as directory
    int dh = smbc_opendir(smb_url.c_str());
    if (dh == -1) {
        // Not a directory or cannot open - try unlink as file
        if (smbc_unlink(smb_url.c_str()) == 0) {
            printf("DeleteRecursive: unlinked file '%s'\n", smb_url.c_str());
            return true;
        }
        printf("DeleteRecursive: opendir and unlink failed for '%s' (errno=%d)\n", smb_url.c_str(), errno);
        return false;
    }

    struct smbc_dirent *ent;
    while ((ent = smbc_readdir(dh)) != NULL) {
        if (strcmp(ent->name, ".") == 0 || strcmp(ent->name, "..") == 0) continue;

        std::string child_remote = remotePath.empty() ? std::string(ent->name)
                                                      : remotePath + "/" + ent->name;

        // If directory, recurse; otherwise unlink the file
        if (ent->smbc_type == SMBC_DIR) {
            if (!DeleteRecursive(server, share, child_remote, username, password)) {
                smbc_closedir(dh);
                return false;
            }
        } else {
            std::string child_url = "smb://" + server + "/" + share + "/" + child_remote;
            if (smbc_unlink(child_url.c_str()) == -1) {
                printf("DeleteRecursive: unlink failed '%s' (errno=%d)\n", child_url.c_str(), errno);
                smbc_closedir(dh);
                return false;
            }
            printf("DeleteRecursive: unlinked file '%s'\n", child_url.c_str());
        }
    }

    smbc_closedir(dh);

    // Directory should now be empty; remove it
    if (smbc_rmdir(smb_url.c_str()) == -1) {
        printf("DeleteRecursive: rmdir failed '%s' (errno=%d)\n", smb_url.c_str(), errno);
        return false;
    }

    printf("DeleteRecursive: removed directory '%s'\n", smb_url.c_str());
    return true;
}

// Rename (move) a remote file/directory within the same share.
bool MoveRemote(const std::string& server, const std::string& share,
                const std::string& oldRemotePath, const std::string& newRemotePath,
                const std::string& username, const std::string& password) {
    g_smb_username = username;
    g_smb_password = password;
    smbc_init(auth_fn, 1);

    std::string old_url = "smb://" + server + "/" + share;
    if (!oldRemotePath.empty()) old_url += "/" + oldRemotePath;
    std::string new_url = "smb://" + server + "/" + share;
    if (!newRemotePath.empty()) new_url += "/" + newRemotePath;

    int res = smbc_rename(old_url.c_str(), new_url.c_str());
    if (res == -1) {
        printf("MoveRemote: rename failed '%s' -> '%s' (errno=%d)\n", old_url.c_str(), new_url.c_str(), errno);
        return false;
    }
    printf("MoveRemote: renamed '%s' -> '%s'\n", old_url.c_str(), new_url.c_str());
    return true;
}

#endif
