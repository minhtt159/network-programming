/*
    Sinh vien: Tran Tuan Minh
    MSV: 15021754
    Partially taken from this http://www.hs-augsburg.de/~meixner/prog/html/netzwerke/classes/sockets/EchoMultiServer.java
*/

import java.net.*;
import java.io.*;
import java.nio.*;
import java.nio.file.*;
import java.util.Arrays;

class EchoThread extends Thread {
    // buffer size
   	private int BUFSIZE = 1024;
    private Socket connSock;
    private DataInputStream in;
    private DataOutputStream out;
    private Path initPath;
    private String menu = "1. List\n2. Download <file>\n3. @logout\n";
    private String error = "Unknow command, try again\n";

    // Constructor
   	public EchoThread(Socket connSock) {
        this.connSock = connSock;
        initPath = Paths.get(FileSystems.getDefault().getPath("").toAbsolutePath().toString(), "SharedFolder");
    }

    // Run
    public void run() {
        String bufStr;
        
        try {
            // Prepare I/O for socket
            in  = new DataInputStream(connSock.getInputStream());
            out = new DataOutputStream(connSock.getOutputStream());
            byte[] buffer = new byte[BUFSIZE];
            int numByte;

            while (true){
                // Send menu to client
                out.writeUTF(menu);
                out.flush();

                // Get options
                bufStr = in.readUTF().trim();
                // Debug
                System.out.println(bufStr);

                /*
                1. List 
                2. Download <file>
                3. @logout
                */
                if (bufStr.equals("1") || bufStr.equals("List")) {
                    // List file
                    String thisPath = initPath.toString();

                    // https://stackoverflow.com/questions/5694385/getting-the-filenames-of-all-files-in-a-folder
                    File folder = new File(thisPath);
                    File[] listFile = folder.listFiles();

                    for (int i = 0; i < listFile.length; i++){
                        if (listFile[i].isFile()){
                            out.writeUTF(listFile[i].getName() + "\n");
                            // Debug
                            // System.out.println(listFile[i].getName());
                        }
                    }
                    out.flush();
                }
                else if (bufStr.equals("3") || bufStr.equals("@logout")) {
                    // Exit connection
                    break;
                }
                else {
                    if (bufStr.startsWith("Down") || bufStr.startsWith("down")){
                        // Read file name from client
                        String fileName = bufStr.substring(bufStr.indexOf(" ")).trim();
                        // Debug
                        System.out.println("Client need file: " + fileName);

                        File file = new File(initPath.toString(), fileName);
                        if (!file.exists() || !file.isFile()) {
                            // File not found
                            out.writeUTF("0\n");
                            out.flush();
                            continue;
                        }
                        // Prepare send file
                        FileInputStream fis = new FileInputStream(file);
                        long totalByte = file.length();
                        out.writeUTF(String.valueOf(totalByte) + "\n");
                        out.flush();
                        // Debug
                        System.out.println("File size is:" + totalByte);
                        
                        Arrays.fill(buffer, (byte)0x00);
                        while ((numByte = fis.read(buffer)) != -1) {
                            out.write(buffer, 0, numByte);
                            out.flush();
                            Arrays.fill(buffer, (byte)0x00);
                            // Debug
                            // System.out.println(numByte);
                        }
                        fis.close();
                        System.out.println("Send file done");
                    }
                    else {
                        out.writeUTF(error);
                        out.flush();
                        continue;
                    }
                }
            } 
        }
        catch (EOFException ex){
            // Read no input
        }
        catch (SocketException ex) {
            // output thread closed the socket
        }
        catch (IOException ex) {
            ex.printStackTrace();
        }
        // While end, close connection
        System.out.println("Connection closed");
        if (!connSock.isClosed()){
            try {
                connSock.close();
            }
            catch (IOException ex) {
                ex.printStackTrace();
            }
        }
    }
}

public class Server{
    public static int port = 1509;

    public static void main(String[] args) {
        if (args.length == 1){
            port = Integer.parseInt(args[0]);
        }
        try {
            // Open socket with default addr at port (Listen all), backlog = 1
            ServerSocket servSocket = new ServerSocket(port, 1);
            System.out.println("Socket ready: " + servSocket);

            while (true) { 
                // Create client socket
                Socket connection = servSocket.accept();
                System.out.println("Connection established with "  + connection); 
                Thread echoThread = new EchoThread(connection);
                echoThread.start();
                // Wait
            }
            // servSocket.close();
        }
        catch (IOException ex) {
            ex.printStackTrace();
        }
        // catch (InvalidPathException ex){
        //     System.err.println(ex.getMessage());
        // }
    }
} 
