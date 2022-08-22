CFLAGS  = -V -mmcs51 --model-large --xram-size 0x1800 --xram-loc 0x0000 --code-size 0xec00 --stack-auto --Werror -I../chlib/src
CC      = sdcc
FLASHER = ../CH55x_python_flasher/chflasher.py
TARGET  = jasmin
OBJS	= main.rel controller.rel hid.rel hid_guncon3.rel hid_keyboard.rel hid_switch.rel hid_xbox.rel settings.rel ch559.rel flash.rel led.rel serial.rel timer3.rel usb_host.rel

all: $(TARGET).bin

program: $(TARGET).bin
	$(FLASHER) -w -f $(TARGET).bin

run: program
	$(FLASHER) -s

clean:
	rm -f *.asm *.lst *.rel *.rst *.sym $(TARGET).bin $(TARGET).ihx $(TARGET).lk $(TARGET).map $(TARGET).mem

%.rel: %.c ../chlib/src/*.h *.h
	$(CC) -c $(CFLAGS) -I../ $<

%.rel: ../chlib/src/%.c ../chlib/src/*.h
	$(CC) -c $(CFLAGS) $<

$(TARGET).ihx: $(OBJS) 
	$(CC) $(CFLAGS) $(OBJS) -o $@

%.bin: %.ihx
	sdobjcopy -I ihex -O binary $< $@
