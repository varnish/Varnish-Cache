<?xml version="1.0" encoding="utf-8"?>
<!--
  This XSL stylesheet will transform a DocBook XML document into
  XHTML 1.0 Transitional.  It does not cover the entire DocBook
  schema, and is primarily meant for previewing DocBook documents
  in a browser.
  -->
<xsl:stylesheet
    version="1.0"
    xmlns="http://www.w3.org/1999/xhtml"
    xmlns:xsl="http://www.w3.org/1999/XSL/Transform">

  <xsl:output
      method="xml" encoding="utf-8" indent="yes"
      doctype-public="-//W3C//DTD XHTML 1.0 Transitional//EN"
      doctype-system="http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd"/>

  <xsl:template match="/book">
    <html>
      <head>
	<title>
	  <xsl:call-template name="book-title"/>
	</title>
	<link rel="stylesheet" type="text/css" href="docbook-html.css"/>
      </head>
      <body>
	<h1 class="book-title">
	  <xsl:call-template name="book-title"/>
	</h1>
	<xsl:apply-templates select="chapter|appendix|bibliography"/>
      </body>
    </html>
  </xsl:template>

  <xsl:template match="/article">
    <html>
      <head>
	<title>
	  <xsl:call-template name="article-title"/>
	</title>
	<link rel="stylesheet" type="text/css" href="docbook-html.css"/>
      </head>
      <body>
	<h1 class="article-title">
	  <xsl:call-template name="article-title"/>
	</h1>
	<xsl:apply-templates select="section|appendix|bibliography"/>
      </body>
    </html>
  </xsl:template>

  <xsl:template match="chapter">
    <xsl:param name="level" select="1"/>
    <div class="chapter">
      <xsl:apply-templates>
	<xsl:with-param name="level" select="$level + 1"/>
      </xsl:apply-templates>
    </div>
  </xsl:template>

  <xsl:template match="section">
    <xsl:param name="level" select="1"/>
    <div class="section">
      <xsl:apply-templates>
	<xsl:with-param name="level" select="$level + 1"/>
      </xsl:apply-templates>
    </div>
  </xsl:template>

  <xsl:template match="appendix">
    <xsl:param name="level" select="1"/>
    <div class="appendix">
      <xsl:apply-templates>
	<xsl:with-param name="level" select="$level + 1"/>
      </xsl:apply-templates>
    </div>
  </xsl:template>

  <xsl:template match="bibliography">
    <xsl:param name="level" select="1"/>
    <div class="bibliography">
      <xsl:apply-templates>
	<xsl:with-param name="level" select="$level + 1"/>
      </xsl:apply-templates>
    </div>
  </xsl:template>

  <xsl:template name="article-title">
    <xsl:choose>
      <xsl:when test="articleinfo/title">
	<xsl:value-of select="articleinfo/title"/>
      </xsl:when>
      <xsl:when test="title">
	<xsl:value-of select="title"/>
      </xsl:when>
      <xsl:otherwise>
	<xsl:text>Unnamed DocBook Article</xsl:text>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:template>

  <xsl:template name="book-title">
    <xsl:choose>
      <xsl:when test="bookinfo/title">
	<xsl:value-of select="bookinfo/title"/>
      </xsl:when>
      <xsl:when test="title">
	<xsl:value-of select="title"/>
      </xsl:when>
      <xsl:otherwise>
	<xsl:text>Unnamed DocBook Book</xsl:text>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:template>

  <xsl:template match="title">
    <xsl:param name="level" select="1"/>
    <xsl:element name="{concat('h', $level)}">
      <xsl:attribute name="class">
	<xsl:value-of select="concat('title', $level)"/>
      </xsl:attribute>
      <xsl:apply-templates/>
    </xsl:element>
  </xsl:template>

  <xsl:template match="itemizedlist">
    <ul>
      <xsl:apply-templates/>
    </ul>
  </xsl:template>

  <xsl:template match="orderedlist">
    <ol>
      <xsl:apply-templates/>
    </ol>
  </xsl:template>

  <xsl:template match="listitem">
    <li>
      <xsl:apply-templates/>
    </li>
  </xsl:template>

  <xsl:template match="informaltable">
    <div class="informaltable">
      <xsl:apply-templates/>
    </div>
  </xsl:template>

  <xsl:template match="tgroup">
    <table>
      <xsl:apply-templates/>
    </table>
  </xsl:template>

  <xsl:template match="thead">
    <thead>
      <xsl:apply-templates/>
    </thead>
  </xsl:template>

  <xsl:template match="tbody">
    <tbody>
      <xsl:apply-templates/>
    </tbody>
  </xsl:template>

  <xsl:template match="tfoot">
    <tfoot>
      <xsl:apply-templates/>
    </tfoot>
  </xsl:template>

  <xsl:template match="row">
    <tr>
      <xsl:apply-templates/>
    </tr>
  </xsl:template>

  <xsl:template match="entry">
    <td>
      <xsl:apply-templates/>
    </td>
  </xsl:template>

  <xsl:template match="para">
    <p>
      <xsl:apply-templates/>
    </p>
  </xsl:template>

  <xsl:template match="*" priority="-1">
    <!--xsl:message>Warning: no template for element <xsl:value-of select="name()"/></xsl:message-->
    <xsl:value-of select="concat('&lt;', name(), '&gt;')"/>
    <xsl:apply-templates/>
    <xsl:value-of select="concat('&lt;/', name(), '&gt;')"/>
  </xsl:template>
</xsl:stylesheet>
