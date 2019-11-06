/*
    Sinh vien: Tran Tuan Minh
    MSV: 15021754
    Partially taken from this http://www.hs-augsburg.de/~meixner/prog/html/netzwerke/classes/sockets/EchoTest.java
*/

import java.net.*;
import java.io.*;
import java.nio.*;
import java.nio.file.*;
import java.util.Arrays;
import java.util.Scanner;
import java.util.concurrent.TimeUnit;

public class Client {
	static int port = 1509;
	static int BUFSIZE = 1024;
	static String host = "127.0.0.1";
	public static void main(String[] args) {
		// Usage
		if (args.length == 0){
			System.err.println("Usage: java Client host[:port] <bufsize>");
			System.exit(-1);
		}
		// Read host
		String hostPort = args[0];
		int i = hostPort.indexOf(":");
		if (i == -1){
			host = hostPort;
		}
		else {
			host = hostPort.substring(0,i);
			port = Integer.parseInt(hostPort.substring(i+1));
		}
		// Read buffer size
		// if (args.length == 2) {
		// 	BUFSIZE = Integer.parseInt(args[1]);
		// }
		// else {
		// 	System.err.println("Something not right");
		// 	System.exit(-1);
		// }

		// Init variables
		Socket sockfd		= null;
		DataInputStream in	= null;
		DataOutputStream out= null;
		byte[] buffer = new byte[BUFSIZE];
		int numByte;
		String bufStr;
		Scanner input = new Scanner(System.in);

		try {
			// Connect to host
			sockfd = new Socket(host, port);

			// Prepare I/O for socket
			in = new DataInputStream(sockfd.getInputStream());
			out = new DataOutputStream(sockfd.getOutputStream());
		}
		catch (UnknownHostException e) {
			System.err.println("Don't know about host: " + host);
		} 
		catch (IOException e) {
			System.err.println("Couldn't get I/O for the connection to: " + host);
		}
		if (sockfd != null && in != null && out != null){
			// Loop
			while (true) {
				try{
					// Get menu from server
					bufStr = in.readUTF();
					System.out.print(bufStr);

					// Get option from client
					String cmd = input.nextLine();
					// Debug
					// System.out.println(cmd);
					
					/*
					1. List 
					2. Download <file>
					3. @logout
					*/
					// Send option to server
					if (cmd.equals("1") || cmd.equals("List")){
						out.writeUTF(cmd + "\n");
						bufStr = in.readUTF();
						System.out.print(bufStr);
					}
					else if (cmd.equals("3") || cmd.equals("@logout")){
						out.writeUTF(cmd + "\n");
						break;
					}
					else if (cmd.startsWith("Down") || cmd.startsWith("down")){
						// Prepare to take file
						String filePath = FileSystems.getDefault().getPath("").toAbsolutePath().toString();
						String fileName = cmd.substring(cmd.indexOf(" ")).trim();
						File file = new File(filePath, fileName);

						// Check if file exist
						try {
							Files.deleteIfExists(Paths.get(file.getPath()));
							System.out.println("File exist, trying to delete");
						}
						catch (NoSuchFileException e){
							// ok
						}

						out.writeUTF(cmd + "\n");
						// Read file length
						bufStr = in.readUTF().trim();
						int fileLength = Integer.parseInt(bufStr);
						if (fileLength == 0){
							// File not found
							System.out.println("File not found on server");
							continue;
						}

						// Debug
						System.out.println("Writing to file " + file.toString());

						FileOutputStream fos = new FileOutputStream(file);
						Arrays.fill(buffer, (byte)0x00);
						while (fileLength > 0) {
							numByte = in.read(buffer, 0, Math.min(fileLength, BUFSIZE));	// read just enough byte
							fileLength -= numByte;
							fos.write(buffer, 0, numByte);
						}
						fos.close();

						// Finish
						System.out.println("Received file");
					}
					else {
						out.writeUTF(cmd + "\n");
						bufStr = in.readUTF();
						System.out.print(bufStr);
					}
				}
				catch (IOException ex) {
					ex.printStackTrace();
					break;
				}
			}
			if (!sockfd.isClosed()){
				try {
					sockfd.close();
				}
				catch (IOException ex) {
					ex.printStackTrace();
				}
			}
		}
		input.close();
	}
}
