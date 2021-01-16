#pragma pack(1) /* avoid alignment - match disk structure */
struct load_hdr_record
{
	uint8_t		Code_flags;		/* 0x00 */
	uint16_t	Code_size;		/* 0x01 */
	uint16_t	Init_data_size;	/* 0x03 */
	uint16_t	Const_size;		/* 0x05 */
	uint16_t	Const_checksum;	/* 0x07 */
	uint16_t	Stack_size;		/* 0x09 */
	uint16_t	Code_spare;		/* 0x0b */
};

struct load_data_record
{
	uint8_t		Load_type;
	uint16_t	Offset;
	uint16_t	Length;
	uint8_t		data[];
};
#pragma pack()

