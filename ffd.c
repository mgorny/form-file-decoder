/**
 * form-file-decoder
 * (c) 2014 Michał Górny
 * 2-clause BSD license
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>

const int not_reached = 0;

int file_no = 0;

char* memstr(char* buf, const char* needle, size_t buf_len)
{
	char* p = buf;
	size_t l = buf_len;
	size_t needle_len = strlen(needle);

	while (l > 0)
	{
		char* q = memchr(p, needle[0], l);

		if (!q || l < needle_len)
			break;
		if (!memcmp(q, needle, needle_len))
			return q;
		l -= q - p + 1;
		p = q + 1;
	}

	return NULL;
}

int process_file(FILE* f)
{
	char boundary[BUFSIZ];
	char buf[BUFSIZ];

	int had_content_disposition;
	FILE* outf;

	size_t file_size, buf_filled;

	/* Structure:
	 *
	 * <boundary> \r\n
	 * <header1>: <data1> \r\n
	 * <header2>: <data2> \r\n
	 * \r\n
	 * <data>
	 * \r\n
	 */

	/* read the boundary */
	if (!fgets(boundary + 2, sizeof(boundary) - 2, f))
	{
		if (ferror(f))
		{
			fprintf(stderr, "fgets() failed: %s\n", strerror(errno));
			return 1;
		}
		if (feof(f))
		{
			fprintf(stderr, "empty file.\n");
			return 0;
		}

		assert(not_reached);
	}

	/* future occurences will be prefixed with \r\n */
	boundary[0] = '\r';
	boundary[1] = '\n';

	boundary[strlen(boundary) - 2] = 0;

	while (1)
	{
		had_content_disposition = 0;

		/* read headers */
		while (1)
		{
			if (!fgets(buf, sizeof(buf), f))
			{
				if (ferror(f))
				{
					fprintf(stderr, "fgets() failed while reading headers: %s\n", strerror(errno));
					return 1;
				}
				if (feof(f))
				{
					fprintf(stderr, "EOF while reading headers.\n");
					return 1;
				}

				assert(not_reached);
			}

			if (!strcmp(buf, "\r\n"))
				break;
			else if (!strncasecmp(buf, "content-disposition:", 20))
			{
				char* p = buf + 20;
				p += strspn(p, " \t");

				if (strncasecmp(p, "form-data;", 10))
				{
					fprintf(stderr, "Invalid Content-Disposition (not form-data): %s\n",
							p);
					return 1;
				}

				p = strstr(p, "filename=\"");
				if (!p)
				{
					/* no filename, use number instead */
					char fnbuf[20];
					sprintf(fnbuf, "unnamed.%08x", file_no++);

					fprintf(stderr, "%s ...", fnbuf);
					outf = fopen(fnbuf, "wb");
				}
				else
				{
					char* fnbuf;
					int len;
					p += 10;
					len = strcspn(p, "\"");

					if (len == 0)
						outf = NULL;
					else
					{
						fnbuf = malloc(len + 10);
						if (!fnbuf)
						{
							fprintf(stderr, "malloc(%d) failed.\n", len+10);
							return 1;
						}
						memcpy(fnbuf, p, len);
						fnbuf[len] = 0;

						/* find a unique name */
						if (access(fnbuf, F_OK) == 0)
						{
							char* post_fnbuf = fnbuf + len;
							int i = 0;

							do
							{
								sprintf(post_fnbuf, ".%d", i++);
							}
							while (access(fnbuf, F_OK) == 0);
						}

						fprintf(stderr, "%s ...", fnbuf);
						outf = fopen(fnbuf, "wb");

						free(fnbuf);
					}
				}

				had_content_disposition = 1;
			}
			else if (!strncasecmp(buf, "content-type:", 13))
			{
			}
			else if (!strncasecmp(buf, "content-transfer-encoding:", 26))
			{
				fprintf(stderr, "Unsupported %s", buf);
				return 1;
			}
			else
			{
				fprintf(stderr, "Unknown header: %s", buf);
				return 1;
			}
		}

		if (!had_content_disposition)
		{
			fprintf(stderr, "No Content-Disposition, invalid file.\n");
			return 1;
		}

		file_size = 0;
		buf_filled = 0;

		/* copy the data */
		while (!feof(f) || buf_filled > 0)
		{
			size_t n = fread(buf + buf_filled, 1, sizeof(buf) - buf_filled, f);
			size_t wn, wr;
			char* end;

			if (ferror(f))
			{
				fprintf(stderr, "Read error: %s\n", strerror(errno));
				if (outf)
					fclose(outf);
				return 1;
			}

			buf_filled += n;

			/* look for the delimiter */
			end = memstr(buf, boundary, buf_filled);
			if (end)
				wn = end - buf;
			else if (feof(f))
				wn = buf_filled;
			else
			{
				assert(buf_filled >= BUFSIZ / 2);
				wn = BUFSIZ / 2;
			}

			if (wn > 0)
			{
				if (!outf)
				{
					fprintf(stderr, "Non-empty data when empty file expected\n");
					return 1;
				}

				wr = fwrite(buf, 1, wn, outf);
				if (wr == 0)
				{
					fprintf(stderr, "fwrite() failed: %s\n", strerror(errno));
					return 1;
				}

				file_size += wr;
				buf_filled -= wr;

				memmove(buf, buf + wr, buf_filled);
			}

			/* written up to the boundary, rewind the file */
			if (end && !memcmp(buf, boundary, strlen(boundary)))
			{
				if (fseek(f, -buf_filled + 2, SEEK_CUR) != 0)
				{
					fprintf(stderr, "fseek() failed: %s\n",
							strerror(errno));
					fclose(outf);
					return 1;
				}
				break;
			}
		}

		if (outf)
		{
			fclose(outf);
			fprintf(stderr, " %zu\n", file_size);
		}

		/* (re-)read the next boundary line */
		if (!fgets(buf, sizeof(buf), f))
		{
			if (ferror(f))
			{
				fprintf(stderr, "fgets() failed: %s\n", strerror(errno));
				return 1;
			}
			if (feof(f))
			{
				fprintf(stderr, "premature EOF when looking for boundary.\n");
				return 0;
			}

			assert(not_reached);
		}

		/* EOF */
		if (!strcmp(buf + strlen(boundary + 2), "--\r\n"))
			return 0;
	};

	return 0;
}

int main(int argc, char* argv[])
{
	int i;

	if (argc < 2)
	{
		fprintf(stderr, "Usage: %s <file>...\n", argv[0]);
		return 1;
	}

	for (i = 1; i < argc; ++i)
	{
		FILE* f;

		f = fopen(argv[i], "rb");
		if (!f)
		{
			fprintf(stderr, "Unable to open %s: %s\n",
					argv[i], strerror(errno));
			return 1;
		}
		fprintf(stderr, "[%s]\n", argv[i]);

		process_file(f);

		if (f != stdin)
			fclose(f);
	}

	return 0;
}
