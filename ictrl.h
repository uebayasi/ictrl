/*
 * Common control message header.
 * A message can consist of up to 3 parts with specified length.
 */
struct ctrlmsghdr {
	u_int16_t	type;
	u_int16_t	len[3];
};

struct ctrldata {
	void		*buf;
	size_t		 len;
};

#define CTRLARGV(x...)	((struct ctrldata []){ x })
