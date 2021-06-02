// $Id: cxi.cpp,v 1.5 2021-05-18 01:32:29-07 - - $

#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <fstream>
#include <sstream>
using namespace std;

#include <libgen.h>
#include <sys/types.h>
#include <unistd.h>

#include "debug.h"
#include "logstream.h"
#include "protocol.h"
#include "socket.h"

logstream outlog (cout);
struct cxi_exit: public exception {};

unordered_map<string,cxi_command> command_map {
   {"exit", cxi_command::EXIT},
   {"help", cxi_command::HELP},
   {"ls"  , cxi_command::LS  },
   {"get" , cxi_command::GET },
   {"put" , cxi_command::PUT },
   {"rm"  , cxi_command::RM  },
};

static const char help[] = R"||(
exit         - Exit the program.  Equivalent to EOF.
get filename - Copy remote file to local host.
help         - Print help summary.
ls           - List names of files on remote server.
put filename - Copy local file to remote host.
rm filename  - Remove file from remote server.
)||";

void cxi_help() {
   cout << help;
}

void cxi_ls (client_socket& server) {
   cxi_header header;
   header.command = cxi_command::LS;
   DEBUGF ('h', "sending header " << header << endl);
   send_packet (server, &header, sizeof header);
   recv_packet (server, &header, sizeof header);
   DEBUGF ('h', "received header " << header << endl);
   if (header.command != cxi_command::LSOUT) {
      outlog << "sent LS, server did not return LSOUT" << endl;
      outlog << "server returned " << header << endl;
   }else {
      size_t host_nbytes = ntohl (header.nbytes);
      auto buffer = make_unique<char[]> (host_nbytes + 1);
      recv_packet (server, buffer.get(), host_nbytes);
      DEBUGF ('h', "received " << host_nbytes << " bytes");
      buffer[host_nbytes] = '\0';
      cout << buffer.get();
   }
}

void cxi_get(client_socket& server,string filename) {
  cxi_header header;
  header.command = cxi_command::GET;
  strcpy(header.filename, filename.c_str());
  send_packet(server, &header, sizeof header);
  recv_packet(server, &header, sizeof header);
  //Might not be ACK
  if(header.command != cxi_command::FILEOUT) {
    outlog << "sent GET, server did not return FILEOUT" << endl;
    outlog << "server returned " << header << endl;
  } else {
    size_t nbytes = ntohl(header.nbytes);
    auto buffer = make_unique<char[]> (nbytes+1);
    recv_packet (server, buffer.get(), header.nbytes);
    buffer[header.nbytes] = '\0';
    ofstream ofs;
    ofs.open(filename, ofstream::out);
    ofs.write(buffer.get(),header.nbytes);
    ofs.close();
  }
}

void cxi_rm(client_socket& server, string filename) {
  cxi_header header;
  header.command = cxi_command::RM;
  strcpy(header.filename, filename.c_str());
  send_packet(server, &header, sizeof header);
  recv_packet(server, &header, sizeof header);
  if(header.command != cxi_command::ACK) {
    outlog << "sent RM, server did not return ACK" << endl;
    outlog << "server returned " << header << endl;
  } else {
    
  }
}

void cxi_put(client_socket& server, string filename) {
  cxi_header header;
  ifstream ifs;
  ifs.open(filename, ifstream::in);
  if (ifs.fail()) {
    outlog << filename << ": " << strerror (errno) << endl;
    return;
  }

  string get_output;
  ifs.seekg(0, ifs.end);
  size_t nbytes = ifs.tellg();
  ifs.seekg(0, ifs.beg);
  auto buffer = make_unique<char[]>(nbytes);
  ifs.read(buffer.get(), nbytes);
  get_output.append(buffer.get());
  ifs.close();

  header.command = cxi_command::PUT;
  header.nbytes = htonl (nbytes);
  strcpy(header.filename, filename.c_str());
  send_packet(server, &header, sizeof header);
  send_packet(server, get_output.c_str(), nbytes);
}


void usage() {
   cerr << "Usage: " << outlog.execname() << " host port" << endl;
   throw cxi_exit();
}

pair<string,in_port_t> scan_options (int argc, char** argv) {
   for (;;) {
      int opt = getopt (argc, argv, "@:");
      if (opt == EOF) break;
      switch (opt) {
         case '@': debugflags::setflags (optarg);
                   break;
      }
   }
   if (argc - optind != 2) usage();
   string host = argv[optind];
   in_port_t port = get_cxi_server_port (argv[optind + 1]);
   return {host, port};
}

int main (int argc, char** argv) {
   outlog.execname (basename (argv[0]));
   outlog << to_string (hostinfo()) << endl;
   try {
      auto host_port = scan_options (argc, argv);
      string host = host_port.first;
      in_port_t port = host_port.second;
      outlog << "connecting to " << host << " port " << port << endl;
      client_socket server (host, port);
      outlog << "connected to " << to_string (server) << endl;
      for (;;) {
         string line;
         getline (cin, line);
         if (cin.eof()) throw cxi_exit();
         outlog << "command " << line << endl;

         size_t found = line.find(" ");
         string temp;
         if(found != string::npos) {
           temp = line.substr(0,found);
         } else {
           temp = line;
         }
         
         //const auto& itor = command_map.find (line);
         const auto& itor = command_map.find(temp);
         cxi_command cmd = itor == command_map.end()
                         ? cxi_command::ERROR : itor->second;
         string filename = "";

         if(cmd == cxi_command::GET || cmd == cxi_command::RM
            || cmd == cxi_command::PUT) {
           size_t ind = line.find(" ");
           filename = line.substr(ind+1, line.length());
         }
         switch (cmd) {
            case cxi_command::EXIT:
               throw cxi_exit();
               break;
            case cxi_command::HELP:
               cxi_help();
               break;
            case cxi_command::LS:
               cxi_ls (server);
               break;
            case cxi_command::GET:
               cxi_get (server, filename);
               break;
            case cxi_command::PUT:
               cxi_put (server, filename);
               break;
            case cxi_command::RM:
              cxi_rm (server, filename);
              break;
            default:
               outlog << line << ": invalid command" << endl;
               break;
         }
      }
   }catch (socket_error& error) {
      outlog << error.what() << endl;
   }catch (cxi_exit& error) {
      DEBUGF ('x', "caught cxi_exit");
   }
   return 0;
}

