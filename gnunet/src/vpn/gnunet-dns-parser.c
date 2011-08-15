#include "platform.h"
#include "gnunet-dns-parser.h"
#include "gnunet-vpn-packet.h"

/**
 * Parse a name from DNS to a normal .-delimited, 0-terminated string.
 *
 * @param d The destination of the name. Should have at least 255 bytes allocated.
 * @param src The DNS-Packet
 * @param idx The offset inside the Packet from which on the name should be read
 * @returns The offset of the first unparsed byte (the byte right behind the name)
 */
static unsigned int
parse_dns_name (char *d, const unsigned char *src, unsigned short idx)
{                               /*{{{ */
  char *dest = d;

  int len = src[idx++];

  while (len != 0)
  {
    if (len & 0xC0)
    {                           /* Compressed name, offset in this and the next octet */
      unsigned short offset = ((len & 0x3F) << 8) | src[idx++];

      parse_dns_name (dest, src, offset - 12);  /* 12 for the Header of the DNS-Packet, idx starts at 0 which is 12 bytes from the start of the packet */
      return idx;
    }
    memcpy (dest, src + idx, len);
    idx += len;
    dest += len;
    *dest = '.';
    dest++;
    len = src[idx++];
  };
  *dest = 0;

  return idx;
}

/*}}}*/

/**
 * Parse a complete DNS-Record from raw DNS-data to a struct dns_record
 *
 * @param data The DNS-data
 * @param dst Pointer to count pointers; individual pointers will be allocated
 * @param count Number of records to parse
 * @param idx The offset inside the Packet from which on the name should be read
 * @returns The offset of the first unparsed byte (the byte right behind the last record)
 */
static unsigned short
parse_dns_record (unsigned char *data,  /*{{{ */
                  struct dns_record **dst,
                  unsigned short count, unsigned short idx)
{
  int i;
  unsigned short _idx;

  for (i = 0; i < count; i++)
  {
    dst[i] = GNUNET_malloc (sizeof (struct dns_record));
    dst[i]->name = alloca (255);        // see RFC1035, no name can be longer than this.
    char *name = dst[i]->name;

    _idx = parse_dns_name (name, data, idx);
    dst[i]->namelen = _idx - idx;

    dst[i]->name = GNUNET_malloc (dst[i]->namelen);
    memcpy (dst[i]->name, name, dst[i]->namelen);

    idx = _idx;

    dst[i]->type = *((unsigned short *) (data + idx));
    idx += 2;
    dst[i]->class = *((unsigned short *) (data + idx));
    idx += 2;
    dst[i]->ttl = *((unsigned int *) (data + idx));
    idx += 4;
    dst[i]->data_len = *((unsigned short *) (data + idx));
    idx += 2;
    dst[i]->data = GNUNET_malloc (ntohs (dst[i]->data_len));
    memcpy (dst[i]->data, data + idx, ntohs (dst[i]->data_len));
    idx += ntohs (dst[i]->data_len);
  }
  return idx;
}                               /*}}} */

/**
 * Parse a raw DNS-Packet into an usable struct
 */
struct dns_pkt_parsed *
parse_dns_packet (struct dns_pkt *pkt)
{                               /*{{{ */
  struct dns_pkt_parsed *ppkt = GNUNET_malloc (sizeof (struct dns_pkt_parsed));

  memcpy (&ppkt->s, &pkt->s, sizeof pkt->s);

  unsigned short qdcount = ntohs (ppkt->s.qdcount);
  unsigned short ancount = ntohs (ppkt->s.ancount);
  unsigned short nscount = ntohs (ppkt->s.nscount);
  unsigned short arcount = ntohs (ppkt->s.arcount);

  ppkt->queries = GNUNET_malloc (qdcount * sizeof (struct dns_query *));
  ppkt->answers = GNUNET_malloc (ancount * sizeof (struct dns_record *));
  ppkt->nameservers = GNUNET_malloc (nscount * sizeof (struct dns_record *));
  ppkt->additional = GNUNET_malloc (arcount * sizeof (struct dns_record *));

  unsigned short idx = 0, _idx; /* This keeps track how far we have parsed the data */

  /* Parse the Query */
  int i;

  for (i = 0; i < qdcount; i++)
  {                             /*{{{ */
    ppkt->queries[i] = GNUNET_malloc (sizeof (struct dns_query));
    char *name = alloca (255);  /* see RFC1035, it can't be more than this. */

    _idx = parse_dns_name (name, pkt->data, idx);
    ppkt->queries[i]->namelen = _idx - idx;
    idx = _idx;

    ppkt->queries[i]->name = GNUNET_malloc (ppkt->queries[i]->namelen);
    memcpy (ppkt->queries[i]->name, name, ppkt->queries[i]->namelen);

    ppkt->queries[i]->qtype = *((unsigned short *) (pkt->data + idx));
    idx += 2;
    ppkt->queries[i]->qclass = *((unsigned short *) (pkt->data + idx));
    idx += 2;
  }
  /*}}} */
  idx = parse_dns_record (pkt->data, ppkt->answers, ancount, idx);
  idx = parse_dns_record (pkt->data, ppkt->nameservers, nscount, idx);
  idx = parse_dns_record (pkt->data, ppkt->additional, arcount, idx);
  return ppkt;
}                               /*}}} */

void
free_parsed_dns_packet (struct dns_pkt_parsed *ppkt)
{
  unsigned short qdcount = ntohs (ppkt->s.qdcount);
  unsigned short ancount = ntohs (ppkt->s.ancount);
  unsigned short nscount = ntohs (ppkt->s.nscount);
  unsigned short arcount = ntohs (ppkt->s.arcount);

  int i;

  for (i = 0; i < qdcount; i++)
  {
    GNUNET_free (ppkt->queries[i]->name);
    GNUNET_free (ppkt->queries[i]);
  }
  GNUNET_free (ppkt->queries);
  for (i = 0; i < ancount; i++)
  {
    GNUNET_free (ppkt->answers[i]->name);
    GNUNET_free (ppkt->answers[i]->data);
    GNUNET_free (ppkt->answers[i]);
  }
  GNUNET_free (ppkt->answers);
  for (i = 0; i < nscount; i++)
  {
    GNUNET_free (ppkt->nameservers[i]->name);
    GNUNET_free (ppkt->nameservers[i]->data);
    GNUNET_free (ppkt->nameservers[i]);
  }
  GNUNET_free (ppkt->nameservers);
  for (i = 0; i < arcount; i++)
  {
    GNUNET_free (ppkt->additional[i]->name);
    GNUNET_free (ppkt->additional[i]->data);
    GNUNET_free (ppkt->additional[i]);
  }
  GNUNET_free (ppkt->additional);
  GNUNET_free (ppkt);
}
