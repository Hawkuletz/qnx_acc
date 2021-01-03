#pragma pack(1) /* avoid alignment - match disk structure */
struct load_hdr_record
{
	uint8_t		Code_flags;
	uint16_t	Code_size;
	uint16_t	Init_data_size;
	uint16_t	Const_size;
	uint16_t	Const_checksum;
	uint16_t	Stack_size;
	uint16_t	Code_spare;
};

struct load_data_record
{
	uint8_t		Load_type;
	uint16_t	Offset;
	uint16_t	Length;
	uint8_t		data[];
};
#pragma pack()

