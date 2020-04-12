#include "browser_gui.hpp"

#include "../../ipc/tune.h"

#include "list_items.hpp"

namespace {

    bool ListItemTextCompare(const tsl::elm::ListItem *_lhs, const tsl::elm::ListItem *_rhs){
        return strcasecmp(_lhs->getText().c_str(), _rhs->getText().c_str()) < 0;
    };
}
constexpr const char *const base_path = "/music/";

char path_buffer[FS_MAX_PATH];

BrowserGui::BrowserGui()
    : m_fs(), has_music(), cwd("/") {
    this->m_list = new tsl::elm::List();

    /* Open sd card filesystem. */
    Result rc = fs::OpenSdCardFileSystem(&this->m_fs);
    if (R_SUCCEEDED(rc)) {
        /* Check if base path /music/ exists. */
        IDirectory dir;
        if (R_SUCCEEDED(this->m_fs.OpenDirectoryFormat(&dir, FsDirOpenMode_ReadFiles, base_path))) {
            std::strcpy(this->cwd, base_path);
            this->has_music = true;
        }
        this->scanCwd();
    } else {
        this->m_list->addItem(new tsl::elm::CategoryHeader("Couldn't open SdCard filesystem"));
    }
}

tsl::elm::Element *BrowserGui::createUI() {
    auto rootFrame = new tsl::elm::OverlayFrame("ovl-tune \u266B", VERSION);

    rootFrame->setContent(this->m_list);

    return rootFrame;
}

void BrowserGui::update() {
}
bool BrowserGui::handleInput(u64 keysDown, u64, touchPosition, JoystickPosition, JoystickPosition) {
    if (keysDown & KEY_B) {
        if (this->has_music && this->cwd[7] == '\0') {
            return false;
        } else if (this->cwd[1] != '\0') {
            this->upCwd();
            return true;
        }
    }
    return false;
}

void BrowserGui::scanCwd() {
    tsl::Gui::removeFocus();
    this->m_list->clear();

    /* Show absolute folder path. */
    this->m_list->addItem(new tsl::elm::CategoryHeader(this->cwd, true));

    /* Open directory. */
    IDirectory dir;
    Result rc = this->m_fs.OpenDirectory(&dir, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, this->cwd);
    if (R_SUCCEEDED(rc)) {
        std::vector<tsl::elm::ListItem *> folders, files;

        /* Iternate over directory. */
        for (const auto &elm : DirectoryIterator(&dir)) {
            if (elm.type == FsDirEntryType_Dir) {
                /* Add directory entries. */
                auto *item = new tsl::elm::ListItem(elm.name);
                item->setClickListener([this, item](u64 down) -> bool {
                    if (down & KEY_A) {
                        std::snprintf(this->cwd, FS_MAX_PATH, "%s%s/", this->cwd, item->getText().c_str());
                        this->scanCwd();
                        return true;
                    }
                    return false;
                });
                folders.push_back(item);
            } else if (strcasecmp(elm.name + std::strlen(elm.name) - 4, ".mp3") == 0) {
                /* Add mp3 entries. */
                auto *item = new tsl::elm::ListItem(elm.name);
                item->setClickListener([this, item](u64 down) -> bool {
                    if (down & KEY_A) {
                        std::snprintf(path_buffer, FS_MAX_PATH, "%s%s", this->cwd, item->getText().c_str());
                        tuneEnqueue(path_buffer, TuneEnqueueType_Last);
                        return true;
                    }
                    return false;
                });
                files.push_back(item);
            }
        }
        if (folders.size() == 0 && files.size() == 0) {
            this->m_list->addItem(new tsl::elm::CategoryHeader("Empty..."));
        }

        if (folders.size() > 0) {
            std::sort(folders.begin(), folders.end(), ListItemTextCompare);
            for (auto element : folders)
                this->m_list->addItem(element);
        }
        if (files.size() > 0) {
            this->m_list->addItem(new tsl::elm::CategoryHeader("Files"));
            std::sort(files.begin(), files.end(), ListItemTextCompare);
            for (auto element : files)
                this->m_list->addItem(element);
        }
    } else {
        this->m_list->addItem(new tsl::elm::ListItem("something went wrong :/"));
    }
}

void BrowserGui::upCwd() {
    size_t length = std::strlen(this->cwd);
    if (length <= 1)
        return;

    for (size_t i = length - 2; i >= 0; i--) {
        if (this->cwd[i] == '/') {
            this->cwd[i + 1] = '\0';
            this->scanCwd();
            return;
        }
    }
}
