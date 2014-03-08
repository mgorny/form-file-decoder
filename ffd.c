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

#include <getopt.h>

const int not_reached = 0;

int file_no = 0;
int list_only = 0;
const char* output_dir = ".";

struct option opts[] = {
	{ "list", no_argument, NULL, 'l' },
	{ "output-dir", required_argument, NULL, 'o' },

	{ "help", no_argument, NULL, 'h' },
	{ "version", no_argument, NULL, 'V' },

	{ NULL, 0, NULL, 0 }
};

const char* opts_short = "lo:hV";

const char* help_msg = "Usage: %s [<options>] <file>...\n"
	"\n"
	"Options:\n"
	" -l, --list             list files without extracting them\n"
	" -o, --output-dir <DIR> store output files in specified directory\n"
	"\n"
	" -h, --help             this help message\n"
	" -V, --version          program version\n";


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
	int no_output;
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
		no_output = 0;

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
					char* fnbuf = malloc(strlen(output_dir) + 1 + 8 + 8 + 1);
					if (!fnbuf)
					{
						fprintf(stderr, "malloc() for fnbuf failed: %s\n",
								strerror(errno));
						return 1;
					}

					sprintf(fnbuf, "%s/unnamed.%08x", output_dir, file_no++);

					fprintf(stderr, "%s ...", fnbuf);
					if (!list_only)
					{
						outf = fopen(fnbuf, "wb");

						if (!outf)
						{
							fprintf(stderr, "\nUnable to open %s for writing: %s\n",
									fnbuf, strerror(errno));
							free(fnbuf);
							return 1;
						}
					}

					free(fnbuf);
				}
				else
				{
					char* fnbuf;
					int len;
					p += 10;
					len = strcspn(p, "\"");

					if (len == 0)
						no_output = 1;
					else
					{
						fnbuf = malloc(strlen(output_dir) + 1 + len + 10);
						if (!fnbuf)
						{
							fprintf(stderr, "malloc() for fnbuf failed.\n");
							return 1;
						}
						sprintf(fnbuf, "%s/", output_dir);
						strncat(fnbuf, p, len);

						/* find a unique name */
						if (!list_only && access(fnbuf, F_OK) == 0)
						{
							char* post_fnbuf = fnbuf + strlen(fnbuf);
							int i = 0;

							do
							{
								sprintf(post_fnbuf, ".%d", i++);
							}
							while (access(fnbuf, F_OK) == 0);
						}

						fprintf(stderr, "%s ...", fnbuf);
						if (!list_only)
						{
							outf = fopen(fnbuf, "wb");

							if (!outf)
							{
								fprintf(stderr, "\nUnable to open %s for writing: %s\n",
										fnbuf, strerror(errno));
								free(fnbuf);
								return 1;
							}
						}

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
				fprintf(stderr, "\nUnsupported %s", buf);
				return 1;
			}
			else
			{
				fprintf(stderr, "\nUnknown header: %s", buf);
				return 1;
			}
		}

		if (!had_content_disposition)
		{
			fprintf(stderr, "\nNo Content-Disposition, invalid file.\n");
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
				fprintf(stderr, "\nRead error: %s\n", strerror(errno));
				if (!no_output && !list_only)
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
				if (no_output)
				{
					fprintf(stderr, "\nNon-empty data when empty file expected\n");
					return 1;
				}

				if (!list_only)
				{

					wr = fwrite(buf, 1, wn, outf);
					if (wr == 0)
					{
						fprintf(stderr, "\nfwrite() failed: %s\n", strerror(errno));
						return 1;
					}
				}
				else /* list_only */
					wr = wn;

				file_size += wr;
				buf_filled -= wr;

				memmove(buf, buf + wr, buf_filled);
			}

			/* written up to the boundary, rewind the file */
			if (end && !memcmp(buf, boundary, strlen(boundary)))
			{
				if (fseek(f, -buf_filled + 2, SEEK_CUR) != 0)
				{
					fprintf(stderr, "\nfseek() failed: %s\n",
							strerror(errno));
					if (!no_output && !list_only)
						fclose(outf);
					return 1;
				}
				break;
			}
		}

		if (!no_output)
		{
			if (!list_only)
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
	int opt;
	int i;

	while ((opt = getopt_long(argc, argv, opts_short, opts, NULL)) != -1)
	{
		switch (opt)
		{
			case 'l':
				list_only = 1;
				break;
			case 'o':
				output_dir = optarg;
				break;

			case 'V':
				printf("form-file-decoder 0\n");
				return 0;
			case 'h':
				printf(help_msg, argv[0]);
				return 0;
			default:
				printf(help_msg, argv[0]);
				return 1;
		}
	}

	if (access(output_dir, W_OK) != 0)
	{
		fprintf(stderr, "Output directory not writable: %s\n",
				strerror(errno));
		return 1;
	}

	if (argc - optind < 1)
	{
		fprintf(stderr, "Usage: %s <file>...\n", argv[0]);
		return 1;
	}

	for (i = optind; i < argc; ++i)
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
