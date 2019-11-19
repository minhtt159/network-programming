### Message Type:
```
message MessageType {
	enum Message {
		HOSTINFO = 1;
		FILEINFO = 2;
		FILEDATA = 3;
		COMMONREPLY = 4;
	}
	required Message message = 1;
	required uint64 timeStamp = 2;
}
```
Trường này dùng để mổ tả message chính trao đổi qua lại giữa các peer

|Nội dung|Diễn giải|Ghi chú|
|-----------|-----------|-----------|
|message|loại message tiếp theo| |
|timeStamp|thời gian gửi message| |

### Host Info:
```
message HostInfo {
	required string host = 1;
	required uint32 port = 2;
	required bool isServer = 3;
	required uint64 timeStamp = 4;
}
```
Trường này để lưu thông tin của các peer

|Nội dung|Diễn giải|Ghi chú|
|-----------|-----------|-----------|
|host|listen address của peer| |
|port|listen port của peer| |
|isServer|host này là server hay client| |
|timeStamp|lần cuối nhận HostInfo| |

### Client Info:
```
message ClientInfo {
	required string remoteHost = 1;
	required uint32 localPort = 2;
	repeated HostInfo peer = 3;
}
```

Trường này để lưu thông tin các peer trong mạng tại mỗi client

|Nội dung|Diễn giải|Ghi chú|
|-----------|-----------|-----------|
|remoteHost|ip của máy được gửi đến| |
|localPort|listen port của client gửi gói tin này| |
|peer|mảng các HostInfo lưu thông tin các peer| | 

### Message structure:

Tất cả các message gửi lên server sẽ có cấu trúc như sau

|Message type size (varint encoded)|MessageType|Message size (varint encoded)|Message|
|-----------|-----------|-----------|-----------|
|Trường này chứa size của message type, varint encoded|MessageType được định nghĩa phía trên|Trường này chứa size của message chính, varint encoded|Message chính được gửi lên cho server|

### Bài toán:

- Server: S1

- Client: C1, C2, C3

- Server cần truyền 2 file (1-10MB) đến Client(s)

- Người dùng nhập tên file cần truyền tại Server, hiện thời gian (1)

- Sau khi Client(s) nhận được hết file, hiện thời gian (2)

- Client(s) có thể gửi dữ liệu cho nhau

- Có checksum

- Thời gian = (2) - (1)

### Use case:

1. Các máy đồng thời tạo socket lắng nghe tại port định nghĩa trước, port này dùng chung cho tất cả user (?)

2. Server được bật lên đầu tiên, khởi tạo môi trường mạng

3. Connect
	- Client(s) kết nối đến Server, cập nhật Server vào danh sách ClientInfo
	- Server gửi danh sách ClientInfo, đồng thời cập nhật Client vào danh sách của mình
	- Client(s) nhận danh sách ClientInfo của Server, cập nhật lại ClientInfo của mình

4. File transfer
	- Server in ra danh sách tên file có thể gửi
	- Server nhập tên file, bắt đầu tính thời gian (1)
	- Server gửi thông tin sẽ chuyển file đến Client(s): {tên file, checksum}
	- Server sẽ gửi lần lượt các packet: block[i] -> Client[i%3]

5. Extra
	- Client sau khi nhận được packet, gửi packet đó đến cho Client khác
	- Sau 5s không nhận được block nào từ Server, Client bắt đầu hỏi các packet còn thiếu
	- Khi Client nhận được đủ file, thông báo cho Client khác biết, in ra thời gian (2)
	- Khi tất cả Client nhận được đủ file, đóng Client

### Tham khảo:

https://stackoverflow.com/questions/14998261/2-way-udp-chat-application












