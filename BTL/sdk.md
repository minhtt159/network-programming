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
|host|thông tin của peer host| |
|port|thông tin của peer port| |
|isServer|host này là server hay client| |
|timeStamp|lần cuối nhận HostInfo| |

### Client Info:
```
message ClientInfo{
	optional string host = 1;
	optional uint32 port = 2;
	repeated HostInfo peer = 3;
}
```

Trường này để lưu thông tin các peer trong mạng tại mỗi client

|Nội dung|Diễn giải|Ghi chú|
|-----------|-----------|-----------|
|host|thông tin của client host| |
|port|thông tin của client port| |
|peer|mảng các HostInfo lưu thông tin các peer| | 

### Message structure:

Tất cả các message gửi lên server sẽ có cấu trúc như sau

|Message type size (varint encoded)|MessageType|Message size (varint encoded)|Message|
|-----------|-----------|-----------|-----------|
|Trường này chứa size của message type, varint encoded|MessageType được định nghĩa phía trên|Trường này chứa size của message chính, varint encoded|Message chính được gửi lên cho server|