#include <iostream>
#include <aio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#include <sys/stat.h>

#include <vector>
#include <chrono>

using namespace std;

off_t  block_size = 4096;
int cnt_aio, file_size;

struct aio_operation {
    struct aiocb aio;
    char *buffer;
    int write_operation;
    struct aio_operation* next_operation;
};

void getLastError() {
    const char *error_string = strerror(errno);
    cerr << "Error: " << error_string << endl;
}

void aio_completion_handler(sigval_t sigval) {
    struct aio_operation *aio_op = (struct aio_operation *)sigval.sival_ptr;
    struct aio_operation *next = aio_op->next_operation;

    if (aio_op->write_operation) {
        to_string(*aio_op->buffer).clear();
        // операция записи
        file_size = file_size - aio_return(&aio_op->aio);
        cout<<"Filesize:"<<file_size<<endl;
        if (file_size <= block_size) {
            aio_op->aio.aio_nbytes = file_size;
            next->aio.aio_nbytes = file_size;
        }
        aio_op->aio.aio_offset = aio_op->aio.aio_offset + block_size * cnt_aio;
        next->aio.aio_offset = next->aio.aio_offset + block_size * cnt_aio;
        if (aio_read(&next->aio) == -1) {
            getLastError();
        }
    } else {
        //операция чтения
        if (aio_return(&aio_op->aio) != 0) {
            if (aio_write(&next->aio) == -1) {
                getLastError();
            }
        }
    }
}

string getFileName() {
    string input_name;
    cout << "Enter file name (/home/username/folder/file.txt): ";
    cin >> input_name;
    return input_name;
}

int main() {
    int cnt_block;
    int read_fd, write_fd; /* Файловый дескриптор */
    string read_filename, write_filename;
    struct stat read_stat{};

    cout << "Count data blocks: ";
    cin >> cnt_block;

    cout << "Count asynchronous operation: ";
    cin >> cnt_aio;

    vector<aiocb> aiocb_read_list, aiocb_write_list;
    vector<string> buf_list;
    vector<aio_operation> aio_op_list(cnt_aio * 2);

    block_size = block_size * cnt_block;

    cout << "Read file name" << endl;
    read_filename = getFileName();

    read_fd = open(read_filename.c_str(), O_RDONLY|O_NONBLOCK, 0666);
    if (read_fd == -1) {
        getLastError();
    }
    else {
        cout << "File open successfully!" << endl;

        cout << "Write file name" << endl;
        write_filename = getFileName();

        write_fd = open(write_filename.c_str(), O_CREAT | O_WRONLY | O_TRUNC | O_NONBLOCK, 0666);
        if (write_fd == -1) {
            getLastError();
        }
        else {
            cout << "File create successfully!" << endl;

            fstat(read_fd, &read_stat);

            file_size = read_stat.st_size;

            for (int i = 0; i < cnt_aio * 2; i++) {
                memset(&aio_op_list[i], 0, sizeof(aio_operation));

                if (i % 2 == 0) {
                    aiocb_read_list.push_back(aiocb());
                    memset(&aiocb_read_list[i/2], 0, sizeof(aiocb));
                    aiocb_read_list[i/2].aio_fildes = read_fd;

                    aio_op_list[i].write_operation = 0;
                    aio_op_list[i].next_operation = &aio_op_list[i + 1];

                    buf_list.push_back(string());
                    buf_list[i / 2] = string(block_size, ' ');
                    buf_list[i / 2].clear();

                    aiocb_read_list[i/2].aio_buf = (void *)buf_list[i / 2].c_str();
                    aiocb_read_list[i/2].aio_nbytes = block_size;
                    aiocb_read_list[i/2].aio_offset = block_size * (i / 2);
                    aiocb_read_list[i/2].aio_sigevent.sigev_notify = SIGEV_THREAD;
                    aiocb_read_list[i/2].aio_sigevent.sigev_value.sival_ptr = &aio_op_list[i];
                    aiocb_read_list[i/2].aio_sigevent.sigev_notify_function = aio_completion_handler;
                    aiocb_read_list[i/2].aio_sigevent.sigev_notify_attributes = nullptr;

                    aio_op_list[i].aio = aiocb_read_list[i/2];
                }
                else {
                    aiocb_write_list.push_back(aiocb());
                    memset(&aiocb_write_list[i/2], 0, sizeof(aiocb));
                    aiocb_write_list[i/2].aio_fildes = write_fd;
                    aio_op_list[i].write_operation = 1;

                    aio_op_list[i].next_operation = &aio_op_list[i - 1];

                    aiocb_write_list[i/2].aio_buf = (void *)buf_list[i / 2].c_str();
                    aiocb_write_list[i/2].aio_nbytes = block_size;
                    aiocb_write_list[i/2].aio_offset = block_size * (i / 2);
                    aiocb_write_list[i/2].aio_sigevent.sigev_notify = SIGEV_THREAD;
                    aiocb_write_list[i/2].aio_sigevent.sigev_value.sival_ptr = &aio_op_list[i];
                    aiocb_write_list[i/2].aio_sigevent.sigev_notify_function = aio_completion_handler;
                    aiocb_write_list[i/2].aio_sigevent.sigev_notify_attributes = nullptr;

                    aio_op_list[i].aio = aiocb_write_list[i/2];
                }
                aio_op_list[i].buffer = (char *)buf_list[i / 2].c_str();
            }

            auto start = chrono::high_resolution_clock::now();  // timer start

            for (int i = 0; i < cnt_aio; i = i + 2) {
                if (aio_read(&aio_op_list[i].aio) == -1) {
                    getLastError();
                }
            }

            while (file_size != 0) {
                usleep(10000);
            }

            auto end = chrono::high_resolution_clock::now(); // timer stop

            chrono::duration<double> elapsed = end - start;
            cout << "Coping operation took: " << elapsed.count() << " seconds.\n";

            close(write_fd);
        }

        close(read_fd);
    }

    return 0;
}