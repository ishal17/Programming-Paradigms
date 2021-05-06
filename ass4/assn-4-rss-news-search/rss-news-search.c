#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>

#include "url.h"
#include "bool.h"
#include "urlconnection.h"
#include "streamtokenizer.h"
#include "html-utils.h"

#include "hashset.h"

static void Welcome(const char *welcomeTextFileName);
static void BuildIndices(const char *feedsFileName, hashset* stop_list, hashset* seen_articles, hashset* words_info);
static void ProcessFeed(const char *remoteDocumentName, hashset* stop_list, hashset* seen_articles, hashset* words_info);
static void PullAllNewsItems(urlconnection *urlconn, hashset* stop_list, hashset* words_info, hashset* seen_articles);
static bool GetNextItemTag(streamtokenizer *st);
static void ProcessSingleNewsItem(streamtokenizer *st, hashset* stop_list, hashset* words_info, hashset* seen_articles);
static void ExtractElement(streamtokenizer *st, const char *htmlTag, char dataBuffer[], int bufferLength);
static void ParseArticle(const char *articleTitle, const char *articleDescription, const char *articleURL, hashset* stop_list, hashset* words_info, hashset* seen_articles);
static void ScanArticle(streamtokenizer *st, const char *articleTitle, const char *unused, const char *articleURL, hashset* stop_list, hashset* words_info, hashset* seen_articles);
static void QueryIndices(hashset* stop_list, hashset* words_info);
static void ProcessResponse(const char *word, hashset* stop_list, hashset* words_info);
static bool WordIsWellFormed(const char *word);
static int StringHash(const char *s, int numBuckets);
static int strHashFn(const void* ptr, int numBuckets);
static int strCompFn(const void* adr1, const void* adr2);
static void strFreeFn(void* ptr);
static void readStopWords(hashset* h, const char* filename);

static const char *const kTextDelimiters = " \t\n\r\b!@$%^*()_+={[}]|\\'\":;/?.>,<~`";
static const char *const kNewLineDelimiters = "\r\n";

// returns minimum between 2 numbers
#define MIN(a,b) a>b ? b:a
// initial allocation for vector
#define VECTOR_INIT_ALLOC 5

// structure to store string as a key and vector of values
// we define it's functions below
typedef struct {
  char * key;
  vector* values;
} map_node;

static void map_node_free_fn (void* ptr) {
  free(((map_node*) ptr)->key);
  VectorDispose(((map_node*) ptr)->values);
  free(((map_node*) ptr)->values);
}

static int map_node_cmp_fn (const void* ptr1, const void* ptr2) {
  return strcasecmp(((map_node*) ptr1)->key, ((map_node*) ptr2)->key);
}

static int map_node_hash_fn (const void *ptr, int numBuckets) {
  return StringHash(((map_node*) ptr)->key, numBuckets);
}
// end of functions for map_node

// structure to store article, it's adress and number of appearences of some word in it
// we define it's functions below
typedef struct {
  char* article_name;
  char* article_addr;
  int num;
} vector_node;

static void vector_node_free_fn (void* ptr) {
  free(((vector_node*) ptr)->article_name);
  free(((vector_node*) ptr)->article_addr);
}

static int vector_node_num_cmp_fn (const void* ptr1, const void* ptr2) {
  vector_node* node1 = (vector_node*) ptr1;
  vector_node* node2 = (vector_node*) ptr2;
  return node2->num - node1->num;
}

static int vector_node_name_cmp_fn (const void* ptr1, const void* ptr2) {

  vector_node* node1 = (vector_node*) ptr1;
  vector_node* node2 = (vector_node*) ptr2;
    return strcasecmp(node1->article_addr, node2->article_addr);
}

static int vector_node_hash_fn (const void *ptr, int numBuckets) {
  return strHashFn(((vector_node*) ptr)->article_addr, numBuckets);
}
// end of functions for vector_node


/**
 * Function: main
 * --------------
 * Serves as the entry point of the full application.
 * You'll want to update main to declare several hashsets--
 * one for stop words, another for previously seen urls, etc--
 * and pass them (by address) to BuildIndices and QueryIndices.
 * In fact, you'll need to extend many of the prototypes of the
 * supplied helpers functions to take one or more hashset *s.
 *
 * Think very carefully about how you're going to keep track of
 * all of the stop words, how you're going to keep track of
 * all the previously seen articles, and how you're going to 
 * map words to the collection of news articles where that
 * word appears.
 */
static const char *const kWelcomeTextFile = "data/welcome.txt";
static const char *const filePrefix = "file://";
static const char *const kDefaultFeedsFile = "data/test.txt";

static const int NUM_BUCKETS_BIG = 10007;
static const int NUM_BUCKETS_SMALL = 1009;

static const char *const kStopWordsFile = "data/stop-words.txt";


int main(int argc, char **argv) {
  setbuf(stdout, NULL);
  Welcome(kWelcomeTextFile);

  // create hashset for stop_list words
  hashset stop_list;
  HashSetNew(&stop_list, sizeof(char*), NUM_BUCKETS_BIG, strHashFn, strCompFn, strFreeFn);

  // create hashset for already seen articles
  hashset seen_articles;
  HashSetNew(&seen_articles, sizeof(char*), NUM_BUCKETS_SMALL, strHashFn, strCompFn, strFreeFn);
  readStopWords(&stop_list, kStopWordsFile);

  // create hashset to store all info we are interested in
  hashset words_info;
  HashSetNew(&words_info, sizeof(map_node), NUM_BUCKETS_BIG, map_node_hash_fn, map_node_cmp_fn, map_node_free_fn);

  BuildIndices((argc == 1) ? kDefaultFeedsFile : argv[1], &stop_list, &seen_articles, &words_info);
  QueryIndices(&stop_list, &words_info);

  // don't forget to free allocated memory
  HashSetDispose(&stop_list);
  HashSetDispose(&seen_articles);
  HashSetDispose(&words_info);

  return 0;
}

/**
 * Function: readStopWords
 * --------------
 * Reads stop words from given file
 */

static void readStopWords(hashset* h, const char* filename) {
  FILE *infile;
  streamtokenizer st;
  char buffer[1024];
  
  infile = fopen(filename, "r");
  assert(infile != NULL);    
  
  STNew(&st, infile, kNewLineDelimiters, true);
  while (STNextToken(&st, buffer, sizeof(buffer))) {
    char* word = strdup(buffer);
    HashSetEnter(h, &word);
  }
  STDispose(&st); // remember that STDispose doesn't close the file, since STNew doesn't open one.. 
  fclose(infile);
}


/** 
 * StringHash                     
 * ----------  
 * This function adapted from Eric Roberts' "The Art and Science of C"
 * It takes a string and uses it to derive a hash code, which   
 * is an integer in the range [0, numBuckets).  The hash code is computed  
 * using a method called "linear congruence."  A similar function using this     
 * method is described on page 144 of Kernighan and Ritchie.  The choice of                                                     
 * the value for the kHashMultiplier can have a significant effect on the                            
 * performance of the algorithm, but not on its correctness.                                                    
 * This hash function has the additional feature of being case-insensitive,  
 * hashing "Peter Pawlowski" and "PETER PAWLOWSKI" to the same code.  
 */  
static const signed long kHashMultiplier = -1664117991L;
static int StringHash(const char *s, int numBuckets) {            
  int i;
  unsigned long hashcode = 0;
  
  for (i = 0; i < strlen(s); i++) { 
    hashcode = hashcode * kHashMultiplier + tolower(s[i]);  
  }
  return hashcode % numBuckets;                                
}

// wrapper for string hash function
static int strHashFn(const void* ptr, int numBuckets) {
  return StringHash(*(const char**)ptr, numBuckets);
}

// wrapper for string compare function
static int strCompFn(const void* adr1, const void* adr2) {
  return strcasecmp(*(char**)adr1, *(char**)adr2);
}

// free function for strings
static void strFreeFn (void* ptr) {
  free(*( (char**)ptr ));
}

/** 
 * Function: Welcome
 * -----------------
 * Displays the contents of the specified file, which
 * holds the introductory remarks to be printed every time
 * the application launches.  This type of overhead may
 * seem silly, but by placing the text in an external file,
 * we can change the welcome text without forcing a recompilation and
 * build of the application.  It's as if welcomeTextFileName
 * is a configuration file that travels with the application.
 */
 
static void Welcome(const char *welcomeTextFileName) {
  FILE *infile;
  streamtokenizer st;
  char buffer[1024];
  
  infile = fopen(welcomeTextFileName, "r");
  assert(infile != NULL);    
  
  STNew(&st, infile, kNewLineDelimiters, true);
  while (STNextToken(&st, buffer, sizeof(buffer))) {
    printf("%s\n", buffer);
  }
  
  printf("\n");
  STDispose(&st); // remember that STDispose doesn't close the file, since STNew doesn't open one.. 
  fclose(infile);
}

/**
 * Function: BuildIndices
 * ----------------------
 * As far as the user is concerned, BuildIndices needs to read each and every
 * one of the feeds listed in the specied feedsFileName, and for each feed parse
 * content of all referenced articles and store the content in the hashset of indices.
 * Each line of the specified feeds file looks like this:
 *
 *   <feed name>: <URL of remore xml document>
 *
 * Each iteration of the supplied while loop parses and discards the feed name (it's
 * in the file for humans to read, but our aggregator doesn't care what the name is)
 * and then extracts the URL.  It then relies on ProcessFeed to pull the remote
 * document and index its content.
 */

static void BuildIndices(const char *feedsFileName, hashset* stop_list, hashset* seen_articles, hashset* words_info) {
  FILE *infile;
  streamtokenizer st;
  char remoteFileName[1024];
  
  infile = fopen(feedsFileName, "r");
  assert(infile != NULL);
  STNew(&st, infile, kNewLineDelimiters, true);
  while (STSkipUntil(&st, ":") != EOF) { // ignore everything up to the first semicolon of the line
    STSkipOver(&st, ": ");		 // now ignore the semicolon and any whitespace directly after it
    STNextToken(&st, remoteFileName, sizeof(remoteFileName));   
    ProcessFeed(remoteFileName, stop_list, seen_articles, words_info);
  }
  STDispose(&st);
  fclose(infile);
  printf("\n");
}


/**
 * Function: ProcessFeedFromFile
 * ---------------------
 * ProcessFeed locates the specified RSS document, from locally
 */

static void ProcessFeedFromFile(char *fileName, hashset* stop_list, hashset* seen_articles, hashset* words_info) {
  FILE *infile;
  streamtokenizer st;
  char articleDescription[1024];
  articleDescription[0] = '\0';
  infile = fopen((const char *)fileName, "r");
  assert(infile != NULL);
  STNew(&st, infile, kTextDelimiters, true);
  ScanArticle(&st, (const char *)fileName, articleDescription, (const char *)fileName, stop_list, seen_articles, words_info);
  STDispose(&st); // remember that STDispose doesn't close the file, since STNew doesn't open one..
  fclose(infile);
}

/**
 * Function: ProcessFeed
 * ---------------------
 * ProcessFeed locates the specified RSS document, and if a (possibly redirected) connection to that remote
 * document can be established, then PullAllNewsItems is tapped to actually read the feed.  Check out the
 * documentation of the PullAllNewsItems function for more information, and inspect the documentation
 * for ParseArticle for information about what the different response codes mean.
 */

static void ProcessFeed(const char *remoteDocumentName, hashset* stop_list, hashset* seen_articles, hashset* words_info) {

  if(!strncmp(filePrefix, remoteDocumentName, strlen(filePrefix))){
    ProcessFeedFromFile((char *)remoteDocumentName + strlen(filePrefix), stop_list, words_info, seen_articles);
    return;
  }

  url u;
  urlconnection urlconn;
  
  URLNewAbsolute(&u, remoteDocumentName);
  URLConnectionNew(&urlconn, &u);
  
  switch (urlconn.responseCode) {
      case 0: printf("Unable to connect to \"%s\".  Ignoring...", u.serverName);
              break;
      case 200: PullAllNewsItems(&urlconn, stop_list, words_info, seen_articles);
                break;
      case 301: 
      case 302: ProcessFeed(urlconn.newUrl, stop_list, seen_articles, words_info);
                break;
      default: printf("Connection to \"%s\" was established, but unable to retrieve \"%s\". [response code: %d, response message:\"%s\"]\n",
		      u.serverName, u.fileName, urlconn.responseCode, urlconn.responseMessage);
	       break;
  };
  
  URLConnectionDispose(&urlconn);
  URLDispose(&u);
}

/**
 * Function: PullAllNewsItems
 * --------------------------
 * Steps though the data of what is assumed to be an RSS feed identifying the names and
 * URLs of online news articles.  Check out "datafiles/sample-rss-feed.txt" for an idea of what an
 * RSS feed from the www.nytimes.com (or anything other server that syndicates is stories).
 *
 * PullAllNewsItems views a typical RSS feed as a sequence of "items", where each item is detailed
 * using a generalization of HTML called XML.  A typical XML fragment for a single news item will certainly
 * adhere to the format of the following example:
 *
 * <item>
 *   <title>At Installation Mass, New Pope Strikes a Tone of Openness</title>
 *   <link>http://www.nytimes.com/2005/04/24/international/worldspecial2/24cnd-pope.html</link>
 *   <description>The Mass, which drew 350,000 spectators, marked an important moment in the transformation of Benedict XVI.</description>
 *   <author>By IAN FISHER and LAURIE GOODSTEIN</author>
 *   <pubDate>Sun, 24 Apr 2005 00:00:00 EDT</pubDate>
 *   <guid isPermaLink="false">http://www.nytimes.com/2005/04/24/international/worldspecial2/24cnd-pope.html</guid>
 * </item>
 *
 * PullAllNewsItems reads and discards all characters up through the opening <item> tag (discarding the <item> tag
 * as well, because once it's read and indentified, it's been pulled,) and then hands the state of the stream to
 * ProcessSingleNewsItem, which handles the job of pulling and analyzing everything up through and including the </item>
 * tag. PullAllNewsItems processes the entire RSS feed and repeatedly advancing to the next <item> tag and then allowing
 * ProcessSingleNewsItem do process everything up until </item>.
 */

static void PullAllNewsItems(urlconnection *urlconn, hashset* stop_list, hashset* words_info, hashset* seen_articles) {
  streamtokenizer st;
  STNew(&st, urlconn->dataStream, kTextDelimiters, false);
  while (GetNextItemTag(&st)) { // if true is returned, then assume that <item ...> has just been read and pulled from the data stream
    ProcessSingleNewsItem(&st, stop_list, words_info, seen_articles);
  }
  
  STDispose(&st);
}

/**
 * Function: GetNextItemTag
 * ------------------------
 * Works more or less like GetNextTag below, but this time
 * we're searching for an <item> tag, since that marks the
 * beginning of a block of HTML that's relevant to us.  
 * 
 * Note that each tag is compared to "<item" and not "<item>".
 * That's because the item tag, though unlikely, could include
 * attributes and perhaps look like any one of these:
 *
 *   <item>
 *   <item rdf:about="Latin America reacts to the Vatican">
 *   <item requiresPassword=true>
 *
 * We're just trying to be as general as possible without
 * going overboard.  (Note that we use strncasecmp so that
 * string comparisons are case-insensitive.  That's the case
 * throughout the entire code base.)
 */

static const char *const kItemTagPrefix = "<item";
static bool GetNextItemTag(streamtokenizer *st) {
  char htmlTag[1024];
  while (GetNextTag(st, htmlTag, sizeof(htmlTag))) {
    if (strncasecmp(htmlTag, kItemTagPrefix, strlen(kItemTagPrefix)) == 0) {
      return true;
    }
  }	 
  return false;
}

/**
 * Function: ProcessSingleNewsItem
 * -------------------------------
 * Code which parses the contents of a single <item> node within an RSS/XML feed.
 * At the moment this function is called, we're to assume that the <item> tag was just
 * read and that the streamtokenizer is currently pointing to everything else, as with:
 *   
 *      <title>Carrie Underwood takes American Idol Crown</title>
 *      <description>Oklahoma farm girl beats out Alabama rocker Bo Bice and 100,000 other contestants to win competition.</description>
 *      <link>http://www.nytimes.com/frontpagenews/2841028302.html</link>
 *   </item>
 *
 * ProcessSingleNewsItem parses everything up through and including the </item>, storing the title, link, and article
 * description in local buffers long enough so that the online new article identified by the link can itself be parsed
 * and indexed.  We don't rely on <title>, <link>, and <description> coming in any particular order.  We do asssume that
 * the link field exists (although we can certainly proceed if the title and article descrption are missing.)  There
 * are often other tags inside an item, but we ignore them.
 */

static const char *const kItemEndTag = "</item>";
static const char *const kTitleTagPrefix = "<title";
static const char *const kDescriptionTagPrefix = "<description";
static const char *const kLinkTagPrefix = "<link";
static void ProcessSingleNewsItem(streamtokenizer *st, hashset* stop_list, hashset* words_info, hashset* seen_articles) {
  char htmlTag[1024];
  char articleTitle[1024];
  char articleDescription[1024];
  char articleURL[1024];                                                        
  articleTitle[0] = articleDescription[0] = articleURL[0] = '\0';
  
  while (GetNextTag(st, htmlTag, sizeof(htmlTag)) && (strcasecmp(htmlTag, kItemEndTag) != 0)) {
    if (strncasecmp(htmlTag, kTitleTagPrefix, strlen(kTitleTagPrefix)) == 0) ExtractElement(st, htmlTag, articleTitle, sizeof(articleTitle));
    if (strncasecmp(htmlTag, kDescriptionTagPrefix, strlen(kDescriptionTagPrefix)) == 0) ExtractElement(st, htmlTag, articleDescription, sizeof(articleDescription));
    if (strncasecmp(htmlTag, kLinkTagPrefix, strlen(kLinkTagPrefix)) == 0) ExtractElement(st, htmlTag, articleURL, sizeof(articleURL));
  }
  
  if (strncmp(articleURL, "", sizeof(articleURL)) == 0) return;     // punt, since it's not going to take us anywhere
  ParseArticle(articleTitle, articleDescription, articleURL, stop_list, words_info, seen_articles);
}

/**
 * Function: ExtractElement
 * ------------------------
 * Potentially pulls text from the stream up through and including the matching end tag.  It assumes that
 * the most recently extracted HTML tag resides in the buffer addressed by htmlTag.  The implementation
 * populates the specified data buffer with all of the text up to but not including the opening '<' of the
 * closing tag, and then skips over all of the closing tag as irrelevant.  Assuming for illustration purposes
 * that htmlTag addresses a buffer containing "<description" followed by other text, these three scanarios are
 * handled:
 *
 *    Normal Situation:     <description>http://some.server.com/someRelativePath.html</description>
 *    Uncommon Situation:   <description></description>
 *    Uncommon Situation:   <description/>
 *
 * In each of the second and third scenarios, the document has omitted the data.  This is not uncommon
 * for the description data to be missing, so we need to cover all three scenarious (I've actually seen all three.)
 * It would be quite unusual for the title and/or link fields to be empty, but this handles those possibilities too.
 */
 
static void ExtractElement(streamtokenizer *st, const char *htmlTag, char dataBuffer[], int bufferLength) {
  assert(htmlTag[strlen(htmlTag) - 1] == '>');
  if (htmlTag[strlen(htmlTag) - 2] == '/') return;    // e.g. <description/> would state that a description is not being supplied
  STNextTokenUsingDifferentDelimiters(st, dataBuffer, bufferLength, "<");
  RemoveEscapeCharacters(dataBuffer);
  if (dataBuffer[0] == '<') strcpy(dataBuffer, "");  // e.g. <description></description> also means there's no description
  STSkipUntil(st, ">");
  STSkipOver(st, ">");
}

/** 
 * Function: ParseArticle
 * ----------------------
 * Attempts to establish a network connect to the news article identified by the three
 * parameters.  The network connection is either established of not.  The implementation
 * is prepared to handle a subset of possible (but by far the most common) scenarios,
 * and those scenarios are categorized by response code:
 *
 *    0 means that the server in the URL doesn't even exist or couldn't be contacted.
 *    200 means that the document exists and that a connection to that very document has
 *        been established.
 *    301 means that the document has moved to a new location
 *    302 also means that the document has moved to a new location
 *    4xx and 5xx (which are covered by the default case) means that either
 *        we didn't have access to the document (403), the document didn't exist (404),
 *        or that the server failed in some undocumented way (5xx).
 *
 * The are other response codes, but for the time being we're punting on them, since
 * no others appears all that often, and it'd be tedious to be fully exhaustive in our
 * enumeration of all possibilities.
 */

static void ParseArticle(const char *articleTitle, const char *articleDescription,
       const char *articleURL, hashset* stop_list, hashset* words_info, hashset* seen_articles) {

  url u;
  urlconnection urlconn;
  streamtokenizer st;

  URLNewAbsolute(&u, articleURL);
  URLConnectionNew(&urlconn, &u);
  
  switch (urlconn.responseCode) {
      case 0: printf("Unable to connect to \"%s\".  Domain name or IP address is nonexistent.\n", articleURL);
	      break;
      case 200: printf("Scanning \"%s\" from \"http://%s\"\n", articleTitle, u.serverName);
	      STNew(&st, urlconn.dataStream, kTextDelimiters, false);
        if (HashSetLookup(seen_articles, articleURL) == NULL) {
	    	  ScanArticle(&st, articleTitle, articleDescription, articleURL, stop_list, words_info, seen_articles);
          HashSetEnter(seen_articles, articleURL);
        }
	    	STDispose(&st);
	    	break;
      case 301:
      case 302: // just pretend we have the redirected URL all along, though index using the new URL and not the old one...
        ParseArticle(articleTitle, articleDescription, urlconn.newUrl, stop_list, words_info, seen_articles);
	    	break;
      default: printf("Unable to pull \"%s\" from \"%s\". [Response code: %d] Punting...\n", articleTitle, u.serverName, urlconn.responseCode);
        break;
  }
  
  URLConnectionDispose(&urlconn);
  URLDispose(&u);
}

/**
 * Function: ScanArticle
 * ---------------------
 * Parses the specified article, skipping over all HTML tags, and counts the numbers
 * of well-formed words that could potentially serve as keys in the set of indices.
 * Once the full article has been scanned, the number of well-formed words is
 * printed, and the longest well-formed word we encountered along the way
 * is printed as well.
 *
 * This is really a placeholder implementation for what will ultimately be
 * code that indexes the specified content.
 */

static void ScanArticle(streamtokenizer *st, const char *articleTitle, const char *unused, 
            const char *articleURL, hashset* stop_list, hashset* words_info, hashset* seen_articles) {

  char word[1024];

  while (STNextToken(st, word, sizeof(word))) {
    if (strcasecmp(word, "<") == 0) {
      SkipIrrelevantContent(st); // in html-utls.h
    } else {
      RemoveEscapeCharacters(word);
      if (WordIsWellFormed(word)) {

        char* word_c = word;      
        if (HashSetLookup(stop_list, &word_c) != NULL) continue;

        map_node new_node;
        new_node.key = word;
        map_node* found_node = (map_node*) HashSetLookup(words_info, &new_node);

        if (found_node == NULL) {
          vector* empty_vector = malloc(sizeof(vector));
          VectorNew(empty_vector, sizeof(vector_node), vector_node_free_fn, VECTOR_INIT_ALLOC);
          new_node.values = empty_vector;
          new_node.key = strdup(word);          
          HashSetEnter(words_info, &new_node);
        } 

        found_node = (map_node*) HashSetLookup(words_info, &new_node);        
        vector_node new_article;
        new_article.article_addr = strdup(articleURL);
        new_article.article_name = strdup(articleTitle);
        new_article.num = -1;

        int article_bucket_idx = VectorSearch(found_node->values, &new_article, vector_node_name_cmp_fn, 0, false);
        
        if (article_bucket_idx >= 0) {
          ((vector_node*) VectorNth(found_node->values, article_bucket_idx))->num++;
          free(new_article.article_name);
          free(new_article.article_addr);
        } else {          
          new_article.num = 1;
          VectorAppend(found_node->values, &new_article);        
        }        

      }

    }

  }

}

/** 
 * Function: QueryIndices
 * ----------------------
 * Standard query loop that allows the user to specify a single search term, and
 * then proceeds (via ProcessResponse) to list up to 10 articles (sorted by relevance)
 * that contain that word.
 */

static void QueryIndices(hashset* stop_list, hashset* words_info) {
  char response[1024];
  while (true) {
    printf("Please enter a single search term [enter to break]: ");
    fgets(response, sizeof(response), stdin);
    response[strlen(response) - 1] = '\0';
    if (strcasecmp(response, "") == 0) break;
    ProcessResponse(response, stop_list, words_info);
  }

}

/** 
 * Function: ProcessResponse
 * -------------------------
 * Placeholder implementation for what will become the search of a set of indices
 * for a list of web documents containing the specified word.
 */

static void ProcessResponse(const char *word, hashset* stop_list, hashset* words_info) {
  if (WordIsWellFormed(word)) {
    if (HashSetLookup(stop_list, &word) != NULL) {
      printf("Too common a word to be taken seriously. Try something more specific.\n");
    } else {

      map_node node_to_find;
      char* word_c = word;
      node_to_find.key = word_c;
      map_node* word_bucket = (map_node*) HashSetLookup(words_info, &node_to_find);

      if (word_bucket == NULL) {
        printf("None of today's news articles contain the word \"%s\".\n", word);
      } else {

        vector* stats = word_bucket->values;
        VectorSort(stats, vector_node_num_cmp_fn);
        int till = MIN(10, VectorLength(stats));
        for (int i = 1; i <= till; i++) {
          vector_node* cur_node = (vector_node*) VectorNth(stats, i-1);
          printf("%d.) \"%s\" [search term occurs %d %s]\n\"%s\"\n", i, cur_node->article_name, 
                    cur_node->num, cur_node->num == 1 ? "time" : "times", cur_node->article_addr);
        }

      }

    } 

  } else {
    printf("\tWe won't be allowing words like \"%s\" into our set of indices.\n", word);
  }

  
}

/**
 * Predicate Function: WordIsWellFormed
 * ------------------------------------
 * Before we allow a word to be inserted into our map
 * of indices, we'd like to confirm that it's a good search term.
 * One could generalize this function to allow different criteria, but
 * this version hard codes the requirement that a word begin with 
 * a letter of the alphabet and that all letters are either letters, numbers,
 * or the '-' character.  
 */

static bool WordIsWellFormed(const char *word) {
  int i;
  if (strlen(word) == 0) return true;
  if (!isalpha((int) word[0])) return false;
  for (i = 1; i < strlen(word); i++)
    if (!isalnum((int) word[i]) && (word[i] != '-')) return false; 

  return true;
}
