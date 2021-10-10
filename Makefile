CC			= gcc
CFLAG		= -Wall
PROG_NAME	= client server
BIN_DIR		= ./bin
SRC_DIR		= ./source
BUILD_DIR	= ./build
SRC_LIST	= $(wildcard $(SRC_DIR)/*.c)
OBJ_LIST	= $(BUILD_DIR)/$(notdir $(SRC_LIST:.c=.o))

.PHONY: all clean $(PROG_NAME) compile

all: $(PROG_NAME)

compile:
	$(CC) -c $(CFLAG) $(SRC_LIST) -o $(OBJ_LIST)

$(PROG_NAME): compile
	$(CC) $(OBJ_LIST) -o $(BIN_DIR)/$@

clean:
	rm -f $(BIN_DIR)/$(PROG_NAME) $(BUILD_DIR)/*.o

456