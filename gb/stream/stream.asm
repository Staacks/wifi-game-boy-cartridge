; Video Stream for WiFi Game Boy Cartridge

SECTION	"start",ROM0[$0100]
    nop
    jp	begin


 DB $CE,$ED,$66,$66,$CC,$0D,$00,$0B,$03,$73,$00,$83,$00,$0C,$00,$0D
 DB $00,$08,$11,$1F,$88,$89,$00,$0E,$DC,$CC,$6E,$E6,$DD,$DD,$D9,$99
 DB $BB,$BB,$67,$63,$6E,$0E,$EC,$CC,$DD,$DC,$99,$9F,$BB,$B9,$33,$3E

 DB "VIDEOSTREAM",0,0,0,0     ; Cart name - 15bytes
 DB 0                         ; $143
 DB 0,0                       ; $144 - Licensee code
 DB 0                         ; $146 - SGB Support indicator
 DB 0                         ; $147 - Cart type
 DB 0                         ; $148 - ROM Size
 DB 0                         ; $149 - RAM Size
 DB 1                         ; $14a - Destination code
 DB $33                       ; $14b - Old licensee code
 DB 0                         ; $14c - Mask ROM version
 DB 0                         ; $14d - Complement check
 DW 0                         ; $14e - Checksum


MACRO ReadTile

	;We will load everything from 0x7ffe, so we set this address to bc once as we do not even need the register for anything else
	ld	de, $7ffe		;3 cycles

	;Sleep a while with a little leeway... (Total: n*4+2-1, so for example ReadTile 6 has 25 cycles in this loop)
	ld	a, \1		    ;2 cycles
	.sleep\@:
		dec a				;1 cycle
	jr	nz, .sleep\@	;3 cycles (-1 if not taken)

	;First read to trigger interrupt on ESP8266
	ld	a, [de]			;2 cycles

	ld	c, $41 			; We need to check ff41 here, 2 cycles
	.wait\@:				; Wait for hblank and copy 16 bytes
		ldh	a, [c]
		and	a, %00000011
	jr	nz, .wait\@

	; Load 16 bytes = 1 tile
	REPT 16		
		ld	a, [de]
		ld	[hli], a
	ENDR

    ENDM


MACRO WaitForMode

	ld	c, $41 ;We need to check ff41 here
	.waitMode\@:
		ldh	a, [c]
		and	a, %00000011
		cp  a, \1
    jr	nz, .waitMode\@

	ENDM


MACRO SendJoypadState
	ld	a, %00100000	; Check button keys
	ld [$ff00], a
	ld	a, [$ff00]
	ld	a, [$ff00]
	cpl
	and	%00001111
	swap	a
	ld	b, a

	ld	a, %00010000	; Check direction keys
	ld [$ff00], a
	ld	a, [$ff00]
	ld	a, [$ff00]
	ld	a, [$ff00]
	ld	a, [$ff00]
	ld	a, [$ff00]
	ld	a, [$ff00]
	cpl
	and %00001111
	or b

	ld	hl, $7fff		; Send to 0x7fff
	ld	[hl], a
    REPT 10                ; Wait for the ESP (this is a bit longer than necessary, but we have time and it has to align with the following timing checks or we do not exacly fit into hblank)
        nop
    ENDR
	ld	[hl], a         ; Send again
	ENDM


begin:
    ; no interrupts needed
	di

    ; BG tile palette.
    ; Experienced GB developmers may hate me for this as it is reverse to common use, but I find this much more intuitive for video streams with 00 being black and 11 being white.
	ld	a, %00011011	; Window palette colors
	ld	[$FF47], a

    ; Set scroll registers to zero
	ld	a, 0
	ld	[$FF42], a
	ld	[$FF43], a

    ; Turn off LCD to load tile map
    ld	a, %00010001	;LCD off, BG Tile Data 0x8000, BG ON
	ld	[$ff40], a

	; Load tile map, which simply starts at 0, overflows once somewhere near the middle. We will switch the tile set to 0x8800 halfway through rendering later, so the overflow will point to other tiles.
LINEADDR = $9800
	ld	a, 0
	REPT 18
    	ld	hl, LINEADDR
LINEADDR = LINEADDR + 32
        REPT 20
		    ld	[hli], a
		    inc	a
        ENDR
	ENDR

    ; Turn on LCD with tile data starting at 0x8000
    ld	a, %10010001	;LCD on, BG Tile Data 0x8000, BG ON
	ld	[$ff40], a


loop:

    ;We just have enough time to load one tile in each HBLANK period. VBLANK might yield another 16 tiles, so we would make 144+16 = 160 tiles per LCD refresh
    ;Therefore, we need three complete LCD refreshs in any case to load all 360 tiles, so we can stick to HBLANK loading with 120 tiles per refresh.
	ld	hl, $8000	;First tile goes here

	ld	b, 3 ; Loop over three LCD refreshs
	lcdLoop:

		; Wait for VBLANK
		WaitForMode %00000001

		; Switch back to tileset starting at 0x8000
		ld	a, %10010001	;LCD on, BG Tile Data 0x8000, BG ON
		ld	[$ff40], a

		;Wait for end of vblank
		WaitForMode %00000011

		; Read 73 tiles
		REPT 73
		    ReadTile 6
		ENDR

		; About half-way through. Switch to tileset starting at 0x8800
		ld	a, %10000001	;LCD on, BG Tile Data 0x8800, BG ON
		ld	[$ff40], a

		; Read remaining 47 tiles for a total of 120
		ReadTile 5 ; 4 cycles shorter than usual to account for tileset switch
		REPT 46
		    ReadTile 6
		ENDR

		; End of loop over three LCD refreshes
		ld a, b
		dec b
		dec a
	jp  nz, lcdLoop

	; Send joypad state. This also triggers the buffer swap and next send cycle on the ESP
	SendJoypadState

jp	loop


