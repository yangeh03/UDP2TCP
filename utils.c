#include "utils.h"
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <shlwapi.h>

// 显示文件选择对话框
BOOL OpenFileDialog(char *filename, DWORD filenameSize)
{
    OPENFILENAME ofn;       // 结构体保存文件对话框信息
    char szFile[260] = {0}; // 保存文件名的缓冲区

    // 初始化OPENFILENAME结构体
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL; // 所属窗口的句柄，NULL表示没有父窗口
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "All Files\0*.*\0Text Files\0*.TXT\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    // 显示打开文件对话框
    if (GetOpenFileName(&ofn) == TRUE)
    {
        // 获取当前工作目录
        char currentDir[MAX_PATH];
        GetCurrentDirectory(sizeof(currentDir), currentDir);

        // 将绝对路径转换为相对路径
        if (PathRelativePathTo(
                szFile,     // 缓存相对路径
                currentDir, // 当前工作目录
                FILE_ATTRIBUTE_DIRECTORY,
                ofn.lpstrFile, // 绝对路径
                FILE_ATTRIBUTE_NORMAL))
        {
            strcpy_s(filename, filenameSize, szFile);
        }
        else
        {
            // 如果转换失败，则使用绝对路径
            strcpy_s(filename, filenameSize, ofn.lpstrFile);
        }

        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

